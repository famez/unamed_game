// Escenarios del capítulo 1 y su lógica: partes con objetivos, combates de zona con
// barreras, nodos cromáticos, faroles (puntos de control), objetos con los que interactuar
// y el paso de un escenario al siguiente.

#include "game.h"

#include <algorithm>

namespace {

const uint32_t kWhite = rgba(226, 230, 240), kGlass = rgba(40, 92, 130), kCyan = rgba(40, 220, 255);
const uint32_t kMagenta = rgba(255, 70, 190), kAmber = rgba(255, 190, 60), kLime = rgba(120, 255, 140);
const uint32_t kNeons[] = {kCyan, kMagenta, kAmber, kLime};
const uint32_t kTints[] = {rgba(226, 230, 240), rgba(205, 212, 250), rgba(200, 238, 222), rgba(250, 210, 228),
                           rgba(250, 226, 196), rgba(222, 208, 250)};

AABB area(Vec3 lo, Vec3 hi) { return {lo, hi}; }
bool inside(const AABB& a, Vec3 p) {
    return p.x >= a.min.x && p.x <= a.max.x && p.y >= a.min.y && p.y <= a.max.y && p.z >= a.min.z && p.z <= a.max.z;
}

}  // namespace

// --- Piezas de construcción --------------------------------------------------------------

void Game::addBlock(Vec3 c, Vec3 h, uint32_t col, Material m) { blocks_.push_back({boxFromCenter(c, h), col, m}); }

void Game::addDecor(const Mat4& t, Vec3 c, Vec3 h, uint32_t col, Material m, bool sphere) {
    decor_.push_back({t, c, h, col, m, sphere});
}

// Contorno luminoso de la cara superior de un bloque (cuatro tiras finas).
void Game::addRim(Vec3 c, float hx, float hz, uint32_t col) {
    const Mat4 id = Mat4::identity();
    const float t = 0.025f;
    addDecor(id, c + Vec3{0, 0, hz}, {hx + t, t, t}, col, Material::Glow, false);
    addDecor(id, c - Vec3{0, 0, hz}, {hx + t, t, t}, col, Material::Glow, false);
    addDecor(id, c + Vec3{hx, 0, 0}, {t, t, hz + t}, col, Material::Glow, false);
    addDecor(id, c - Vec3{hx, 0, 0}, {t, t, hz + t}, col, Material::Glow, false);
}

// Edificio: bloque con cornisa, franja de neón en lo alto, cristaleras con repisa luminosa,
// puerta en arco de luz, cartel holográfico y jardineras (`in` apunta hacia la calle).
void Game::addBuilding(float cx, float cz, float hx, float hz, float height, Vec3 in, uint32_t tint, uint32_t neon) {
    const Mat4 id = Mat4::identity();
    addBlock({cx, height * 0.5f, cz}, {hx, height * 0.5f, hz}, tint);
    addDecor(id, {cx, height + 0.12f, cz}, {hx + 0.15f, 0.12f, hz + 0.15f}, kWhite, Material::Stone, false);
    addDecor(id, {cx, height + 0.02f, cz}, {hx + 0.16f, 0.035f, hz + 0.16f}, neon, Material::Glow, false);
    bool longX = std::fabs(in.z) > 0.5f;  // la fachada mira a ±z: su lado largo va en x
    float shortHalf = longX ? hz : hx, longHalf = longX ? hx : hz;
    Vec3 along = longX ? Vec3{1, 0, 0} : Vec3{0, 0, 1};
    Vec3 face = Vec3{cx, 0, cz} + in * (shortHalf + 0.03f);
    auto panel = [&](Vec3 c, float w, float h, uint32_t col, Material m, float depth = 0.05f) {
        addDecor(id, c, longX ? Vec3{w, h, depth} : Vec3{depth, h, w}, col, m, false);
    };
    if (int(std::fabs(cx * 3 + cz)) % 2 == 0)
        addDecor(id, {cx + along.x * longHalf * 0.4f, height + 0.25f, cz + along.z * longHalf * 0.4f}, {1.2f, 0.9f, 1.2f},
                 kWhite, Material::Stone, true);
    else {
        Vec3 a{cx - along.x * longHalf * 0.5f, height, cz - along.z * longHalf * 0.5f};
        addDecor(id, a + Vec3{0, 1.0f, 0}, {0.06f, 1.0f, 0.06f}, kWhite, Material::Metal, false);
        addDecor(id, a + Vec3{0, 2.05f, 0}, {0.12f, 0.12f, 0.12f}, neon, Material::Glow, true);
    }
    panel(face + Vec3{0, 1.15f, 0}, 0.7f, 1.15f, rgba(30, 40, 70), Material::Metal);
    panel(face + Vec3{0, 2.32f, 0} + in * 0.01f, 0.78f, 0.04f, neon, Material::Glow);
    panel(face + Vec3{0, 1.15f, 0} + along * 0.74f + in * 0.01f, 0.04f, 1.15f, neon, Material::Glow);
    panel(face + Vec3{0, 1.15f, 0} - along * 0.74f + in * 0.01f, 0.04f, 1.15f, neon, Material::Glow);
    panel(face + Vec3{0, 3.0f, 0} + in * 0.35f, 0.9f, 0.28f, kNeons[int(std::fabs(cx + cz) + 8) & 3], Material::Glow, 0.015f);
    for (float side : {-1.0f, 1.0f}) {
        if (longHalf < 2.0f) break;  // fachadas estrechas: solo la puerta
        for (float y : {1.7f, height > 5.0f ? 4.0f : -1.0f}) {
            if (y < 0) continue;
            Vec3 w = face + along * (side * longHalf * 0.55f) + Vec3{0, y, 0};
            panel(w, 0.9f, 0.7f, kGlass, Material::Metal);
            panel(w + Vec3{0, -0.72f, 0} + in * 0.02f, 0.9f, 0.025f, neon, Material::Glow);
        }
        Vec3 box = face + along * (side * longHalf * 0.55f) + in * 0.3f + Vec3{0, 0.3f, 0};
        addDecor(id, box, longX ? Vec3{0.9f, 0.3f, 0.25f} : Vec3{0.25f, 0.3f, 0.9f}, kWhite, Material::Stone, false);
        for (int i = 0; i < 5; ++i) {
            Vec3 p = box + along * (-0.68f + 0.34f * float(i)) + Vec3{0, 0.36f, 0};
            addDecor(id, p, {0.16f, 0.13f, 0.16f}, rgba(70, 190, 120), Material::Cloth, true);
            addDecor(id, p + Vec3{0, 0.12f, 0}, {0.06f, 0.05f, 0.06f}, kNeons[(i + int(std::fabs(cx))) & 3], Material::Glow, true);
        }
    }
}

void Game::addTree(Vec3 base, float size, uint32_t leaves) {
    const Mat4 id = Mat4::identity();
    auto shade = [](uint32_t c, float k) {
        auto ch = [&](int s) { return uint32_t(clampf(float((c >> s) & 0xFF) * k, 0, 255)) << s; };
        return ch(0) | ch(8) | ch(16) | (c & 0xFF000000u);
    };
    addBlock(base + Vec3{0, 1.1f * size, 0}, {0.2f * size, 1.1f * size, 0.2f * size}, kWhite, Material::Stone);
    Vec3 top = base + Vec3{0, 2.5f * size, 0};
    addDecor(id, top, Vec3{1.3f, 1.0f, 1.3f} * size, leaves, Material::Cloth, true);
    addDecor(id, top + Vec3{0.5f, 0.6f, 0.2f} * size, Vec3{0.9f, 0.75f, 0.9f} * size, shade(leaves, 1.12f), Material::Cloth, true);
    addDecor(id, top + Vec3{-0.45f, 0.4f, -0.4f} * size, Vec3{0.9f, 0.75f, 0.9f} * size, shade(leaves, 0.88f), Material::Cloth, true);
    for (int i = 0; i < 5; ++i) {
        float a = float(i) * 1.26f;
        addDecor(id, top + Vec3{std::cos(a) * 1.1f, -0.45f + 0.2f * float(i % 2), std::sin(a) * 1.1f} * size,
                 {0.08f, 0.08f, 0.08f}, rgba(255, 245, 160), Material::Glow, true);
    }
}

void Game::addGate(const std::string& id, Vec3 c, Vec3 h, uint32_t color, bool closed) {
    gates_.push_back({id, boxFromCenter(c, h), closed, color, 0});
}

// --- Escenario 1: Plaza del Prisma ---------------------------------------------------------

void Game::buildPlaza() {
    levelName_ = "Plaza del Prisma";
    floorColor_ = rgba(80, 200, 140);
    floorMaterial_ = Material::Grass;
    const Mat4 id = Mat4::identity();
    addDecor(id, {0, 0.004f, 0}, {16.2f, 0.004f, 16.2f}, rgba(205, 210, 226), Material::Ground, false);
    int k = 0;
    for (float x : {-11.0f, 0.0f, 11.0f}) {
        if (x != 0.0f) addBuilding(x, 19, 5.0f, 3.0f, 4.6f + float(k % 3), {0, 0, -1}, kTints[k % 6], kNeons[k & 3]);
        ++k;
        addBuilding(x, -19, 5.0f, 3.0f, 4.6f + float(k % 3), {0, 0, 1}, kTints[k % 6], kNeons[k & 3]);
        ++k;
        addBuilding(19, x, 3.0f, 5.0f, 4.6f + float(k % 3), {-1, 0, 0}, kTints[k % 6], kNeons[k & 3]);
        ++k;
        addBuilding(-19, x, 3.0f, 5.0f, 4.6f + float(k % 3), {1, 0, 0}, kTints[k % 6], kNeons[k & 3]);
        ++k;
    }
    // Puerta del mercado (norte): dos torreones, arco de luz y un pasaje hacia la salida.
    for (float sx : {-1.0f, 1.0f}) {
        addBlock({sx * 3.9f, 3.0f, 19}, {1.4f, 3.0f, 3.0f}, kTints[1]);
        addBlock({sx * 2.9f, 2.0f, 25}, {0.4f, 2.0f, 3.0f}, kWhite);
        addDecor(id, {sx * 2.55f, 2.0f, 19}, {0.05f, 2.0f, 3.0f}, kMagenta, Material::Glow, false);
    }
    addDecor(id, {0, 6.2f, 16.1f}, {2.6f, 0.35f, 0.3f}, kWhite, Material::Stone, false);
    addDecor(id, {0, 5.8f, 15.8f}, {2.4f, 0.05f, 0.05f}, kMagenta, Material::Glow, false);
    addDecor(id, {0, 6.6f, 15.8f}, {1.6f, 0.3f, 0.02f}, kAmber, Material::Glow, false);  // cartel: "MERCADO"
    addBlock({0, 2.0f, 28.4f}, {3.3f, 2.0f, 0.4f}, kWhite);
    addGate("puerta_mercado", {0, 2.0f, 16.2f}, {2.5f, 2.0f, 0.15f}, kMagenta, true);

    for (float sx : {-1.0f, 1.0f})
        for (float sz : {-1.0f, 1.0f}) {
            Vec3 c{sx * 18.5f, 0, sz * 18.5f};
            addBlock(c + Vec3{0, 4.5f, 0}, {2.6f, 4.5f, 2.6f}, kWhite);
            for (int r = 0; r < 3; ++r)
                addDecor(id, c + Vec3{0, 2.5f + 2.5f * float(r), 0}, {2.68f, 0.06f, 2.68f},
                         kNeons[(r + int(sx + 2 * sz + 3)) & 3], Material::Glow, false);
            addDecor(id, c + Vec3{0, 9.0f, 0}, {2.4f, 1.4f, 2.4f}, kWhite, Material::Stone, true);
            addDecor(id, c + Vec3{0, 10.6f, 0}, {0.3f, 0.3f, 0.3f}, kCyan, Material::Glow, true);
        }
    for (float g : {-5.5f, 5.5f})
        for (Vec3 c : {Vec3{g, 1.0f, -17.0f}, Vec3{17.0f, 1.0f, g}, Vec3{-17.0f, 1.0f, g}}) {
            Vec3 half{std::fabs(c.x) > 16 ? 1.3f : 0.6f, 1.0f, std::fabs(c.z) > 16 ? 1.3f : 0.6f};
            addBlock(c, half, kWhite);
            addDecor(id, c + Vec3{0, 1.0f, 0}, half + Vec3{0.02f, 0.04f, 0.02f}, kMagenta, Material::Glow, false);
            addDecor(id, c + Vec3{0, 1.35f, 0}, {half.x * 1.1f, 0.45f, half.z * 1.1f}, rgba(70, 200, 120), Material::Cloth, true);
        }

    // Fuente de luz: pilón, columna con espiral de neón y el prisma flotante (drawLevel).
    addBlock({0, 0.35f, 0}, {2.4f, 0.35f, 2.4f}, kWhite);
    addBlock({0, 1.0f, 0}, {0.4f, 1.0f, 0.4f}, kWhite);
    addBlock({0, 2.05f, 0}, {1.0f, 0.12f, 1.0f}, kWhite);
    addDecor(id, {0, 0.66f, 0}, {2.15f, 0.05f, 2.15f}, rgba(40, 125, 185), Material::Water, false);
    addDecor(id, {0, 2.19f, 0}, {0.88f, 0.03f, 0.88f}, rgba(40, 125, 185), Material::Water, false);
    addRim({0, 0.7f, 0}, 2.4f, 2.4f, kCyan);
    for (int i = 0; i < 10; ++i)
        addDecor(translate({0, 0.2f + 0.18f * float(i), 0}) * rotateY(float(i) * 0.9f), {0.41f, 0, 0}, {0.02f, 0.05f, 0.12f},
                 kMagenta, Material::Glow, false);
    lamps_.push_back({0, 3.0f, 0});

    addBlock({10.5f, 0.45f, 10.5f}, {3.5f, 0.45f, 3.5f}, rgba(214, 222, 240));
    addBlock({10.5f, 0.15f, 6.1f}, {1.5f, 0.15f, 0.3f}, rgba(214, 222, 240));
    addBlock({10.5f, 0.30f, 6.7f}, {1.5f, 0.30f, 0.3f}, rgba(214, 222, 240));
    addRim({10.5f, 0.9f, 10.5f}, 3.5f, 3.5f, kCyan);
    addBlock({-6.5f, 0.45f, 7.0f}, {0.8f, 0.45f, 0.8f}, kWhite);
    addBlock({-8.7f, 1.6f, 9.0f}, {0.8f, 0.15f, 0.8f}, kWhite);
    addBlock({-11.5f, 2.4f, 11.5f}, {2.2f, 0.2f, 2.2f}, kWhite);
    addBlock({-11.5f, 1.1f, 11.5f}, {0.35f, 1.1f, 0.35f}, kWhite);
    addRim({-8.7f, 1.75f, 9.0f}, 0.8f, 0.8f, kMagenta);
    addRim({-11.5f, 2.6f, 11.5f}, 2.2f, 2.2f, kMagenta);
    addRim({-6.5f, 0.9f, 7.0f}, 0.8f, 0.8f, kMagenta);

    addTree({12.0f, 0.9f, 12.5f}, 1.0f, rgba(255, 110, 200));
    addTree({-13.0f, 0, -4.0f}, 1.2f, rgba(170, 110, 255));
    addTree({13.0f, 0, -5.0f}, 1.1f, rgba(255, 120, 170));
    addTree({-4.0f, 0, -13.5f}, 1.0f, rgba(90, 210, 255));
    addTree({4.5f, 0, 13.5f}, 1.1f, rgba(255, 150, 90));
    for (Vec3 p : {Vec3{5.5f, 0, -5.5f}, Vec3{-5.5f, 0, -5.5f}, Vec3{5.5f, 0, 5.0f}, Vec3{-4.0f, 0, 4.0f}}) {
        addBlock(p + Vec3{0, 1.6f, 0}, {0.08f, 1.6f, 0.08f}, kWhite, Material::Metal);
        addDecor(id, p + Vec3{0, 3.35f, 0}, {0.2f, 0.2f, 0.2f}, kCyan, Material::Glow, true);
        addDecor(id, p + Vec3{0, 3.1f, 0}, {0.16f, 0.03f, 0.16f}, kWhite, Material::Metal, false);
        lamps_.push_back(p + Vec3{0, 3.35f, 0});
    }

    for (Vec3 c : {Vec3{-12.0f, 0, -12.0f}, Vec3{-11.0f, 0, -12.5f}, Vec3{-12.7f, 0, -10.9f}, Vec3{8.0f, 0, -12.0f},
                   Vec3{9.0f, 0, -12.4f}, Vec3{-13.0f, 0, 6.0f}})
        crates_.push_back({boxFromCenter(c + Vec3{0, 0.4f, 0}, {0.4f, 0.4f, 0.4f})});
    TreasureChest a;
    a.pos = {13.0f, 0, -12.0f};
    a.yaw = -kPi * 0.75f;
    a.contents = {{ItemId::Potion, 2}, {ItemId::Ether, 1}};
    a.munny = 30;
    TreasureChest b;
    b.pos = {11.5f, 0.9f, 12.6f};
    b.yaw = kPi;
    b.contents = {{ItemId::StarStaff, 1}, {ItemId::HiPotion, 1}};
    TreasureChest c;
    c.pos = {-12.6f, 2.6f, 12.6f};
    c.yaw = kPi * 0.75f;
    c.contents = {{ItemId::BlazeGem, 1}, {ItemId::Elixir, 1}};
    c.munny = 40;
    chests_ = {a, b, c};

    // Nodos cromáticos: uno junto a las cajas, otro en la terraza, otro en la plataforma alta.
    nodes_ = {{"nodo_oeste", {-9.5f, 0, -9.0f}, 11, kMagenta, "nodo_1"},
              {"nodo_terraza", {9.0f, 0.9f, 12.0f}, 11, kAmber, "nodo_2"},
              {"nodo_alto", {-10.6f, 2.6f, 10.6f}, 12, kCyan, "nodo_3"}};
    auto zone = [](Vec3 ctr, float r) { return AABB{ctr - Vec3{r, 1, r}, ctr + Vec3{r, 4, r}}; };
    encounters_ = {
        {"despertar", zone({0, 0, -3}, 30), {{{EnemyKind::Imp, {4, 0, -10}}, {EnemyKind::Imp, {-4, 0, -10}}}}, {}, "primer_combate", ""},
        {"nodo_oeste", zone({-9.5f, 0, -9}, 5), {{{EnemyKind::Imp, {-12, 0, -6}}, {EnemyKind::Imp, {-6, 0, -12}}, {EnemyKind::Imp, {-7, 0, -6}}}},
         {}, "", ""},
        {"nodo_terraza", zone({10, 0, 10}, 5), {{{EnemyKind::Imp, {8, 0.9f, 13}}, {EnemyKind::Shroom, {12, 0.9f, 9}}}}, {}, "domo", ""},
        {"nodo_alto", zone({-10, 0, 9}, 5), {{{EnemyKind::Moth, {-9, 0, 12}}, {EnemyKind::Moth, {-13, 0, 8}}, {EnemyKind::Imp, {-8, 0, 5}}}},
         {}, "eco_enemigo", ""},
    };
    echoes_ = {{{-7.5f, 1.3f, -11.0f}, "recuerdo_alba"}, {{-12.5f, 3.6f, 10.0f}, "recuerdo_promesa"}};

    LevelPart p1;
    p1.title = "Despertar";
    p1.objective = "Defiéndete de los Grises";
    p1.kind = PartKind::Encounter;
    p1.target = "despertar";
    p1.marker = {0, 1, -8};
    p1.sceneDone = "despertar_fin";
    LevelPart p2;
    p2.title = "Los tres nodos";
    p2.objective = "Enciende los nodos cromáticos";
    p2.kind = PartKind::Nodes;
    p2.count = 3;
    p2.marker = nodes_[0].pos;
    p2.sceneDone = "plaza_nodos";
    LevelPart p3;
    p3.title = "La puerta sellada";
    p3.objective = "Lleva la luz del prisma hasta la puerta (F: girar espejos)";
    p3.kind = PartKind::Puzzle;
    p3.target = "receptor_puerta";
    p3.marker = {6, 1.5f, 0};
    p3.sceneDone = "puerta_abierta";
    LevelPart p4;
    p4.title = "La puerta del mercado";
    p4.objective = "Cruza la puerta del mercado";
    p4.kind = PartKind::Exit;
    p4.area = area({-2.5f, -1, 22}, {2.5f, 5, 28});
    p4.marker = {0, 1.5f, 22};
    parts_ = {p1, p2, p3, p4};
    // El haz sale del pie de la fuente hacia el este; tres espejos (mal orientados al
    // principio) pueden llevarlo hasta la puerta.
    emitters_ = {{{0.55f, 1.4f, 0}, {1, 0, 0}, rgba(255, 200, 90)}};
    mirrors_ = {{{6.0f, 0, 0}, 0, 0}, {{6.0f, 0, 12.0f}, 2, 0}, {{0, 0, 12.0f}, 1, 0}};
    receptors_ = {{"receptor_puerta", {0, 1.4f, 15.6f}, 0, "puerta_mercado", ""}};
    heroStart_ = {0, 0, -8};
    heroStartYaw_ = 0;
    nextLevel_ = LevelId::Mercado;
}

// --- Escenario 2: Mercado de los Ecos -------------------------------------------------------

void Game::buildMercado() {
    levelName_ = "Mercado de los Ecos";
    floorColor_ = rgba(196, 200, 214);
    floorMaterial_ = Material::Ground;
    const Mat4 id = Mat4::identity();
    // Calle de los toldos (z 0..28) entre edificios bajos.
    addBlock({0, 3, -2.0f}, {11, 3, 1}, kWhite);
    int k = 0;
    for (float cz : {3.5f, 11.0f, 18.5f, 25.5f}) {
        addBuilding(-7.5f, cz, 3.8f, 3.6f, 4.0f + float(k % 3) * 0.8f, {1, 0, 0}, kTints[k % 6], kNeons[k & 3]);
        ++k;
        addBuilding(7.5f, cz, 3.8f, 3.6f, 4.0f + float((k + 1) % 3) * 0.8f, {-1, 0, 0}, kTints[k % 6], kNeons[(k + 1) & 3]);
        ++k;
    }
    // Puestos con mostrador (sólido) y toldo de colores; cajas de mercancía.
    const uint32_t awnings[] = {rgba(255, 90, 120), rgba(60, 200, 255), rgba(255, 200, 70), rgba(160, 110, 255)};
    int s = 0;
    for (Vec3 st : {Vec3{-2.6f, 0, 6}, Vec3{2.6f, 0, 13}, Vec3{-2.6f, 0, 20}, Vec3{2.6f, 0, 24.5f}}) {
        addBlock(st + Vec3{0, 0.5f, 0}, {0.5f, 0.5f, 1.2f}, rgba(150, 120, 100), Material::Cloth);
        float side = st.x < 0 ? 1.0f : -1.0f;
        addDecor(translate(st + Vec3{side * 0.3f, 2.2f, 0}) * rotateZ(side * 0.35f), {}, {0.9f, 0.04f, 1.4f},
                 awnings[s % 4], Material::Cloth, false);
        addDecor(id, st + Vec3{0, 1.05f, 0}, {0.4f, 0.05f, 1.0f}, kNeons[s & 3], Material::Glow, false);
        ++s;
    }
    crates_.push_back({boxFromCenter({-2.8f, 0.4f, 9.0f}, {0.4f, 0.4f, 0.4f})});
    crates_.push_back({boxFromCenter({2.8f, 0.4f, 17.0f}, {0.4f, 0.4f, 0.4f})});
    // Cables con farolillos apagados cruzando la calle.
    for (float z : {5.0f, 12.0f, 19.0f, 26.0f})
        for (int i = 0; i < 7; ++i) {
            float x = -3.0f + float(i);
            addDecor(id, {x, 4.0f - 0.25f * std::sin(float(i) / 6.0f * kPi), z}, {0.12f, 0.15f, 0.12f}, kNeons[i & 3],
                     Material::Glow, true);
        }

    // Plaza del mercado (z 30..52): edificios alrededor, torre de farolillos en el centro.
    for (float cz : {35.0f, 46.0f}) {
        addBuilding(-15, cz, 3.0f, 5.0f, 5.5f, {1, 0, 0}, kTints[k % 6], kNeons[(k + 1) & 3]);
        addBuilding(15, cz, 3.0f, 5.0f, 5.0f, {-1, 0, 0}, kTints[(k + 1) % 6], kNeons[(k + 2) & 3]);
        k += 2;
    }
    for (float sx : {-1.0f, 1.0f}) {
        addBlock({sx * 7.8f, 2.5f, 29.2f}, {4.2f, 2.5f, 0.9f}, kWhite);   // une la calle con la plaza
        addBuilding(sx * 8.5f, 55, 5.5f, 3.0f, 5.0f, {0, 0, -1}, kTints[k % 6], kNeons[(k + 1) & 3]);
        ++k;
        addBlock({sx * 3.4f, 2, 60}, {0.4f, 2, 4}, kWhite);              // pasaje de salida
    }
    addBlock({0, 2, 64.5f}, {3.8f, 2, 0.5f}, kWhite);
    addTree({-9, 0, 33}, 1.0f, rgba(255, 120, 190));
    addTree({9, 0, 49}, 1.1f, rgba(110, 200, 255));
    // Puesto de Nora: el de los farolillos, con un farolillo grande apagado.
    Vec3 nora{8.5f, 0, 44.5f};
    addBlock(nora + Vec3{0, 0.5f, 0}, {1.4f, 0.5f, 0.6f}, rgba(150, 120, 100), Material::Cloth);
    addDecor(translate(nora + Vec3{0, 2.4f, -0.3f}) * rotateX(-0.3f), {}, {1.7f, 0.04f, 1.0f}, rgba(255, 170, 80), Material::Cloth, false);
    for (int i = 0; i < 5; ++i)
        addDecor(id, nora + Vec3{-1.0f + 0.5f * float(i), 1.25f, 0}, {0.13f, 0.18f, 0.13f}, rgba(255, 200, 120), Material::Glow, true);
    interacts_ = {{"puesto_nora", nora + Vec3{0, 0, -1.0f}, "Examinar el puesto", "nora_puesto"}};

    nodes_ = {{"nodo_mercado", {0, 0, 40}, 14, kAmber, "nodo_mercado"}};
    // Cámara del tesoro (opcional): solo se abre con luz magenta.
    addBlock({-12, 1.5f, 46.6f}, {2.0f, 1.5f, 0.2f}, kWhite);
    addBlock({-12, 1.5f, 51.4f}, {2.0f, 1.5f, 0.2f}, kWhite);
    addBlock({-12, 3.1f, 49}, {2.0f, 0.1f, 2.6f}, kWhite);
    addGate("camara", {-10.1f, 1.5f, 49}, {0.15f, 1.5f, 2.2f}, kMagenta, true);
    emitters_ = {{{10.5f, 1.4f, 31.5f}, {-1, 0, 0}, rgba(255, 200, 90)}};
    mirrors_ = {{{-6.0f, 0, 31.5f}, 0, 0}, {{-6.0f, 0, 47.0f}, 3, kMagenta}};
    receptors_ = {{"receptor_camara", {-9.75f, 1.4f, 47.0f}, kMagenta, "camara", "camara_abierta"}};
    TreasureChest ch;
    ch.pos = {-12, 0, 49};
    ch.yaw = kPi * 0.75f;
    ch.contents = {{ItemId::HiPotion, 1}, {ItemId::Ether, 1}};
    ch.munny = 40;
    chests_ = {ch};

    addGate("calle_atras", {0, 2, 3.0f}, {3.4f, 2, 0.15f}, kMagenta, false);
    addGate("calle_frente", {0, 2, 22.5f}, {1.8f, 2, 0.15f}, kMagenta, false);
    addGate("plaza_entrada", {0, 2, 29.0f}, {3.4f, 2, 0.15f}, kMagenta, false);
    addGate("salida_mercado", {0, 2, 56.6f}, {3.0f, 2, 0.15f}, kAmber, true);
    auto zone = [](Vec3 lo, Vec3 hi) { return AABB{lo, hi}; };
    encounters_ = {
        {"emboscada", zone({-4, -1, 9}, {4, 5, 17}),
         {{{EnemyKind::Moth, {0, 0, 19}}, {EnemyKind::Imp, {-2, 0, 18}}, {EnemyKind::Imp, {2, 0, 7}}},
          {{EnemyKind::Moth, {-2, 0, 8}}, {EnemyKind::Moth, {2, 0, 20}}, {EnemyKind::Imp, {0, 0, 16}}}},
         {"calle_atras", "calle_frente"}, "emboscada", "calle_despejada"},
        {"plaza", zone({-11, -1, 30}, {11, 5, 38}),
         {{{EnemyKind::Imp, {-6, 0, 42}}, {EnemyKind::Imp, {6, 0, 42}}, {EnemyKind::Shroom, {0, 0, 46}}},
          {{EnemyKind::Moth, {-8, 0, 38}}, {EnemyKind::Moth, {8, 0, 38}}, {EnemyKind::Imp, {0, 0, 48}}}},
         {"plaza_entrada"}, "", ""},
        {"jefe_nora", zone({-12, -1, 30}, {12, 5, 52}), {{{EnemyKind::Shroom, {0, 0, 46}, true, "Nora, la que espera"}}}, {"plaza_entrada"}, "", "nora_libre",
         false},
    };
    echoes_ = {{{-1.5f, 1.3f, 15.0f}, "recuerdo_nora"}};

    LevelPart p1;
    p1.title = "La calle de los toldos";
    p1.objective = "Atraviesa la calle";
    p1.kind = PartKind::Encounter;
    p1.target = "emboscada";
    p1.marker = {0, 1, 14};
    p1.sceneStart = "mercado_llegada";
    LevelPart p2;
    p2.title = "La plaza del mercado";
    p2.objective = "Enciende el nodo de la plaza";
    p2.kind = PartKind::Nodes;
    p2.count = 1;
    p2.marker = nodes_[0].pos;
    LevelPart p3;
    p3.title = "Farolillos";
    p3.objective = "Examina el puesto de los farolillos";
    p3.kind = PartKind::Interact;
    p3.target = "puesto_nora";
    p3.marker = nora + Vec3{0, 1.5f, 0};
    LevelPart p4;
    p4.title = "La que espera";
    p4.objective = "Libera a Nora";
    p4.kind = PartKind::Encounter;
    p4.target = "jefe_nora";
    p4.marker = {0, 1, 46};
    p4.openGates = {"salida_mercado"};
    LevelPart p5;
    p5.title = "Hacia el puente";
    p5.objective = "Sal del mercado por el norte";
    p5.kind = PartKind::Exit;
    p5.area = area({-3, -1, 58}, {3, 5, 64});
    p5.marker = {0, 1.5f, 58};
    parts_ = {p1, p2, p3, p4, p5};
    heroStart_ = {0, 0, 0.5f};
    heroStartYaw_ = 0;
    nextLevel_ = LevelId::Puente;
}

// --- Escenario 3: Puente de los Faroles -----------------------------------------------------

void Game::buildPuente() {
    levelName_ = "Puente de los Faroles";
    world_.ground = false;
    const Mat4 id = Mat4::identity();
    // Tramos del puente sobre el vacío (cara superior a la altura `top`).
    auto deck = [&](float x0, float x1, float z0, float z1, float top, uint32_t rim) {
        Vec3 c{(x0 + x1) * 0.5f, top - 0.2f, (z0 + z1) * 0.5f}, h{(x1 - x0) * 0.5f, 0.2f, (z1 - z0) * 0.5f};
        addBlock(c, h, kWhite);
        addRim({c.x, top, c.z}, h.x, h.z, rim);
        addDecor(id, c - Vec3{0, 0.35f, 0}, {h.x * 0.7f, 0.15f, h.z * 0.95f}, kTints[1], Material::Stone, false);
    };
    deck(-7, 7, -10, 1, 0, kCyan);            // orilla de salida
    deck(-1.5f, 1.5f, 2, 10, 0, kCyan);
    deck(-1.5f, 1.5f, 11.6f, 16, 0, kCyan);
    deck(-1.5f, 1.5f, 17.5f, 20, 0.8f, kAmber);
    deck(-1.5f, 1.5f, 21.5f, 26, 0.8f, kAmber);
    deck(-6, 6, 27.5f, 37.5f, 0.8f, kMagenta);  // centro del puente
    deck(-1.5f, 1.5f, 39, 43, 0.8f, kAmber);
    deck(-2.9f, -1.1f, 44.1f, 45.9f, 1.5f, kCyan);  // plataformas flotantes
    deck(0.6f, 2.4f, 46.6f, 48.4f, 2.2f, kCyan);
    deck(-0.9f, 0.9f, 49.6f, 51.4f, 1.6f, kCyan);
    deck(-1.5f, 1.5f, 52.5f, 58, 1.0f, kAmber);
    deck(-8, 8, 59, 76, 1.0f, kMagenta);       // la otra orilla
    // Torres de los tirantes en el centro, con cables de luz.
    for (float sx : {-1.0f, 1.0f})
        for (float sz : {28.5f, 36.5f}) {
            addBlock({sx * 5.4f, 4.8f, sz}, {0.35f, 4.0f, 0.35f}, kWhite);
            addDecor(id, {sx * 5.4f, 8.9f, sz}, {0.18f, 0.18f, 0.18f}, kCyan, Material::Glow, true);
        }
    for (int i = 0; i < 12; ++i) {
        float z = 2 + float(i) * 2.2f;
        addDecor(id, {1.6f, 0.9f + (z > 17 ? 0.8f : 0), z}, {0.02f, 0.02f, 1.0f}, kMagenta, Material::Glow, false);
        addDecor(id, {-1.6f, 0.9f + (z > 17 ? 0.8f : 0), z}, {0.02f, 0.02f, 1.0f}, kMagenta, Material::Glow, false);
    }
    // Faroles (puntos de control) y la silueta de la orilla lejana.
    lanterns_ = {{{1.2f, 0, 8}}, {{1.2f, 0.8f, 24}}, {{5.0f, 0.8f, 36}}, {{1.2f, 1.0f, 56}}};
    addGate("puente_a", {0, 2.3f, 27.3f}, {1.6f, 1.5f, 0.15f}, kMagenta, false);
    addGate("puente_b", {0, 2.3f, 37.7f}, {1.6f, 1.5f, 0.15f}, kMagenta, false);
    TreasureChest ch;
    ch.pos = {-4.5f, 0.8f, 34.5f};
    ch.yaw = kPi * 0.5f;
    ch.contents = {{ItemId::AuroraBlade, 1}};
    ch.munny = 60;
    chests_ = {ch};
    encounters_ = {
        {"puente_centro", AABB{{-6, 0, 28.5f}, {6, 5, 36.5f}},
         {{{EnemyKind::Imp, {-3, 0.8f, 33}}, {EnemyKind::Imp, {3, 0.8f, 33}}, {EnemyKind::Moth, {0, 0.8f, 36}}},
          {{EnemyKind::Moth, {-4, 0.8f, 30}}, {EnemyKind::Moth, {4, 0.8f, 30}}, {EnemyKind::Shroom, {0, 0.8f, 34}}}},
         {"puente_a", "puente_b"}, "puente_combate", "puente_despejado"},
    };
    echoes_ = {{{-2.0f, 2.6f, 45.0f}, "recuerdo_teo"}};

    LevelPart p1;
    p1.title = "Los faroles de Nora";
    p1.objective = "Enciende los faroles del puente";
    p1.kind = PartKind::Lanterns;
    p1.count = 2;
    p1.marker = {1.2f, 1.5f, 24};
    p1.sceneStart = "puente_llegada";
    LevelPart p2;
    p2.title = "El centro del puente";
    p2.objective = "Abre paso en el centro del puente";
    p2.kind = PartKind::Encounter;
    p2.target = "puente_centro";
    p2.marker = {0, 1.8f, 32};
    LevelPart p3;
    p3.title = "La otra orilla";
    p3.objective = "Llega a la otra orilla";
    p3.kind = PartKind::Reach;
    p3.area = area({-8, 0, 62}, {8, 6, 76});
    p3.marker = {0, 2.5f, 64};
    p3.sceneDone = "archivera";
    parts_ = {p1, p2, p3};
    heroStart_ = {0, 0, -5};
    heroStartYaw_ = 0;
    lastLevel_ = true;
}

// --- Carga y lógica ---------------------------------------------------------------------

void Game::loadLevel(LevelId id) {
    saveBrains();  // lo aprendido por los Grises se guarda en cada cambio de escenario
    blocks_.clear();
    decor_.clear();
    lamps_.clear();
    crates_.clear();
    chests_.clear();
    echoes_.clear();
    gates_.clear();
    nodes_.clear();
    lanterns_.clear();
    emitters_.clear();
    mirrors_.clear();
    receptors_.clear();
    beamPaths_.clear();
    beamColors_.clear();
    interacts_.clear();
    encounters_.clear();
    parts_.clear();
    enemies_.clear();
    projectiles_.clear();
    pickups_.clear();
    splashes_.clear();
    rings_.clear();
    blasts_.clear();
    trail_.clear();
    particles_.clear();
    companions_.clear();
    level_ = id;
    part_ = 0;
    activeEncounter_ = -1;
    lock_ = -1;
    lastLevel_ = false;
    world_.ground = true;
    croma_ = cromaShown_ = 0.05f;  // cada escenario empieza casi sin color
    switch (id) {
        case LevelId::Plaza: buildPlaza(); break;
        case LevelId::Mercado: buildMercado(); break;
        case LevelId::Puente: buildPuente(); break;
        case LevelId::Count: break;
    }
    rebuildSolids();
    hero_.body.pos = checkpoint_ = heroStart_;
    hero_.body.vel = {};
    hero_.yaw = checkpointYaw_ = heroStartYaw_;
    briznaPos_ = {};
    briznaVel_ = {};
    partBanner_ = 4.0f;
    if (story_ && !parts_.empty()) playScene(parts_[0].sceneStart);
}

void Game::setGate(const std::string& id, bool closed) {
    for (Gate& g : gates_)
        if (g.id == id && g.closed != closed) {
            g.closed = closed;
            g.openTime = 0;
        }
    rebuildSolids();
}

void Game::spawnEnemy(const Spawn& s) {
    Vec3 to = hero_.body.pos - s.pos;
    Enemy e = makeEnemy(s.kind, s.pos, std::atan2(to.x, to.z), random01() * 10.0f);
    if (s.boss) {  // jefe: mucho más grande y resistente
        e.boss = true;
        e.size = 2.2f;
        e.body.radius *= e.size;
        e.body.height *= e.size;
        e.health = e.def().maxHealth * 7.0f;
        e.title = s.name;
    }
    if (e.def().flying) e.body.pos.y = s.pos.y + e.def().hoverHeight - e.body.height * 0.5f;
    enemies_.push_back(e);
    spawnRing(s.pos + Vec3{0, 0.03f, 0}, 1.2f * e.size, 0.9f, rgba(150, 90, 255));
}

// Si el Prismero cae en mitad de un combate, el combate se reinicia (fuera barreras).
void Game::resetEncounter() {
    if (activeEncounter_ < 0) return;
    Encounter& en = encounters_[size_t(activeEncounter_)];
    en.started = false;
    en.round = -1;
    for (const std::string& b : en.barriers) setGate(b, false);
    enemies_.clear();
    projectiles_.clear();
    lock_ = -1;
    activeEncounter_ = -1;
}

bool Game::partDone(const LevelPart& p) const {
    switch (p.kind) {
        case PartKind::Encounter:
            for (const Encounter& e : encounters_)
                if (e.id == p.target) return e.cleared;
            return true;
        case PartKind::Nodes: {
            int n = 0;
            for (const ColorNode& c : nodes_) n += c.lit;
            return n >= p.count;
        }
        case PartKind::Lanterns: {
            int n = 0;
            for (const Lantern& l : lanterns_) n += l.lit;
            return n >= p.count;
        }
        case PartKind::Interact:
            for (const Interactable& i : interacts_)
                if (i.id == p.target) return i.used;
            return true;
        case PartKind::Reach: return inside(p.area, hero_.body.pos);
        case PartKind::Puzzle:
            for (const Receptor& r : receptors_)
                if (r.id == p.target) return r.lit;
            return true;
        case PartKind::Exit: return false;  // se sale con la transición
    }
    return false;
}

void Game::updateLevel(float dt) {
    partBanner_ = std::max(0.0f, partBanner_ - dt);
    for (Gate& g : gates_) g.openTime += dt;
    for (ColorNode& n : nodes_)
        if (n.lit) n.litTime += dt;
    traceBeams();
    for (Receptor& r : receptors_)
        if (r.lit) r.litTime += dt;
    for (Lantern& l : lanterns_)
        if (l.lit) l.litTime += dt;

    // Transición a otro escenario: fundido a negro, carga y fundido de vuelta.
    if (leaving_) {
        transition_ = std::min(1.0f, transition_ + dt * 1.2f);
        if (transition_ >= 1.0f) {
            leaving_ = false;
            loadLevel(nextLevel_);
        }
        return;
    }
    transition_ = std::max(0.0f, transition_ - dt * 0.9f);

    // Caída al vacío (puente): vuelve al último farol con algo de daño.
    if (!world_.ground && hero_.body.pos.y < -10.0f && hero_.downTimer < 0) {
        hp_ = std::max(1.0f, hp_ - 10.0f);
        hero_.body.pos = checkpoint_ + Vec3{0, 0.5f, 0};
        hero_.body.vel = {};
        heroInvuln_ = 1.5f;
        notify("Has caído al vacío", rgba(255, 140, 160));
    }
    for (Enemy& e : enemies_)
        if (e.alive() && e.body.pos.y < -10.0f) killEnemy(e);

    if (!story_) return;
    // Combates: empiezan al entrar en su zona; las rondas siguen al vencer la anterior.
    for (size_t i = 0; i < encounters_.size(); ++i) {
        Encounter& en = encounters_[i];
        if (!en.started && !en.cleared && en.armed && activeEncounter_ < 0 && hero_.downTimer < 0 &&
            inside(en.trigger, hero_.body.pos)) {
            en.started = true;
            en.round = 0;
            activeEncounter_ = int(i);
            for (const std::string& b : en.barriers) setGate(b, true);
            for (const Spawn& s : en.rounds[0]) spawnEnemy(s);
            uiSound(Sfx::WaveStart, 0.8f);
            playScene(en.sceneStart);
        }
    }
    if (activeEncounter_ >= 0 && !inCombat()) {
        Encounter& en = encounters_[size_t(activeEncounter_)];
        if (en.round + 1 < int(en.rounds.size())) {
            ++en.round;
            for (const Spawn& s : en.rounds[size_t(en.round)]) spawnEnemy(s);
            uiSound(Sfx::WaveStart, 0.6f);
        } else {
            en.cleared = true;
            activeEncounter_ = -1;
            for (const std::string& b : en.barriers) setGate(b, false);
            croma_ = std::min(1.0f, croma_ + 0.04f);
            notify("Zona despejada", rgba(110, 255, 190));
            playScene(en.sceneClear);
        }
    }
    // Partes: al cumplir el objetivo se abren puertas, suena su escena y sigue la siguiente.
    const LevelPart* p = currentPart();
    if (!p) return;
    if (p->kind == PartKind::Exit) {
        if (inside(p->area, hero_.body.pos) && !dialogue_.active()) leaving_ = true;
        return;
    }
    if (partDone(*p)) {
        for (const std::string& g : p->openGates) setGate(g, false);
        playScene(p->sceneDone);
        ++part_;
        partBanner_ = 3.5f;
        if (hero_.body.grounded) {
            checkpoint_ = hero_.body.pos;
            checkpointYaw_ = hero_.yaw;
        }
        if (const LevelPart* next = currentPart()) playScene(next->sceneStart);
        uiSound(Sfx::ItemGet, 0.6f, 0.8f);
    }
}

Game::InteractTarget Game::nearbyInteract() const {
    InteractTarget best;
    float bestD = 1.8f;
    Vec3 hp = hero_.body.pos;
    auto consider = [&](int kind, int index, Vec3 pos, const std::string& label) {
        float d = length(flat(pos - hp));
        if (d < bestD && std::fabs(pos.y - hp.y) < 1.0f) {
            bestD = d;
            best = {kind, index, label};
        }
    };
    for (size_t i = 0; i < chests_.size(); ++i)
        if (!chests_[i].open) consider(0, int(i), chests_[i].pos, "Abrir cápsula");
    for (size_t i = 0; i < nodes_.size(); ++i)
        if (!nodes_[i].lit) consider(1, int(i), nodes_[i].pos, "Encender el nodo cromático");
    for (size_t i = 0; i < lanterns_.size(); ++i)
        if (!lanterns_[i].lit) consider(2, int(i), lanterns_[i].pos, "Encender el farol");
    for (size_t i = 0; i < interacts_.size(); ++i)
        if (!interacts_[i].used) consider(3, int(i), interacts_[i].pos, interacts_[i].label);
    for (size_t i = 0; i < mirrors_.size(); ++i) consider(4, int(i), mirrors_[i].pos, "Girar el espejo");
    return best;
}

void Game::tryInteract() {
    InteractTarget t = nearbyInteract();
    if (t.kind < 0 || hero_.busy()) return;
    if (t.kind == 0) {
        TreasureChest& c = chests_[size_t(t.index)];
        c.open = true;
        uiSound(Sfx::Chest);
        for (const auto& [id, n] : c.contents) {
            inv_.add(id, n);
            const ItemDef& d = itemDef(id);
            std::string text = d.kind == ItemKind::Weapon ? std::string("Nuevo prisma: ") + d.name : std::string("Obtenido: ") + d.name;
            if (n > 1) text += " ×" + std::to_string(n);
            notify(text, d.color);
        }
        Vec3 top = c.pos + Vec3{0, 0.8f, 0};
        dropLoot(top, c.munny, 0, 0, ItemId::Potion, false);
        spawnSparks(top, {0, 1, 0}, 30, rgba(255, 230, 120), 4.0f);
        spawnRing(c.pos + Vec3{0, 0.05f, 0}, 1.5f, 0.6f, rgba(255, 230, 120));
    } else if (t.kind == 1) {
        ColorNode& n = nodes_[size_t(t.index)];
        // Con Grises cerca no se puede: absorberían el color al momento.
        for (const Enemy& e : enemies_)
            if (e.alive() && length(flat(e.body.pos - n.pos)) < 12.0f) {
                notify("Hay Grises demasiado cerca del nodo", rgba(255, 140, 160));
                return;
            }
        n.lit = true;
        n.litTime = 0;
        croma_ = std::min(1.0f, croma_ + 0.18f);
        sound(Sfx::Echo, n.pos);
        uiSound(Sfx::Chest, 0.7f);
        spawnRing(n.pos + Vec3{0, 0.05f, 0}, n.radius, 1.5f, n.color);
        spawnSparks(n.pos + Vec3{0, 1.5f, 0}, {0, 1, 0}, 40, n.color, 5.0f);
        notify("Nodo cromático encendido", n.color);
        playScene(n.scene);
    } else if (t.kind == 2) {
        Lantern& l = lanterns_[size_t(t.index)];
        l.lit = true;
        l.litTime = 0;
        checkpoint_ = l.pos + Vec3{-1.0f, 0, 0};
        croma_ = std::min(1.0f, croma_ + 0.1f);
        uiSound(Sfx::ItemGet, 0.7f, 1.1f);
        spawnSparks(l.pos + Vec3{0, 2.2f, 0}, {0, 1, 0}, 24, rgba(255, 200, 120), 3.0f);
        notify("Farol encendido · punto de control", rgba(255, 200, 120));
    } else if (t.kind == 3) {
        Interactable& it = interacts_[size_t(t.index)];
        it.used = true;
        playScene(it.scene);
    } else {
        Mirror& m = mirrors_[size_t(t.index)];
        m.angle = (m.angle + 1) % 4;  // la normal gira 45° (un espejo tiene dos caras)
        uiSound(Sfx::MenuOk, 0.6f, 0.8f + 0.1f * float(m.angle));
    }
}

// Recorre cada haz: avanza en línea recta, se refleja en los espejos (y toma el color de
// su cristal), se detiene en los sólidos y enciende los receptores que alcanza.
void Game::traceBeams() {
    beamPaths_.assign(emitters_.size(), {});
    beamColors_.assign(emitters_.size(), {});
    std::vector<bool> lit(receptors_.size(), false);
    for (size_t ei = 0; ei < emitters_.size(); ++ei) {
        const LightEmitter& em = emitters_[ei];
        Vec3 p = em.pos, d = normalize(em.dir);
        uint32_t color = em.color;
        std::vector<Vec3>& path = beamPaths_[ei];
        path.push_back(p);
        int lastMirror = -1;
        for (int bounce = 0; bounce < 12; ++bounce) {
            float bestT = 60.0f;
            int hitMirror = -1, hitReceptor = -1;
            for (size_t i = 0; i < mirrors_.size(); ++i) {
                if (int(i) == lastMirror) continue;
                Vec3 c{mirrors_[i].pos.x, p.y, mirrors_[i].pos.z};
                float t = dot(c - p, d);
                if (t > 0.05f && t < bestT && length(p + d * t - c) < 0.45f) bestT = t, hitMirror = int(i);
            }
            for (size_t i = 0; i < receptors_.size(); ++i) {
                float t = dot(receptors_[i].pos - p, d);
                if (t > 0.05f && t < bestT && length(p + d * t - receptors_[i].pos) < 0.5f) {
                    bestT = t;
                    hitReceptor = int(i);
                    hitMirror = -1;
                }
            }
            RayHit h;
            Vec3 end = p + d * bestT;
            if (segmentVsWorld(p, end, world_, h)) {  // algo sólido corta el haz
                path.push_back(p + (end - p) * h.t);
                beamColors_[ei].push_back(color);
                break;
            }
            path.push_back(end);
            beamColors_[ei].push_back(color);
            if (hitReceptor >= 0) {
                const Receptor& r = receptors_[size_t(hitReceptor)];
                if (r.needColor == 0 || r.needColor == color) lit[size_t(hitReceptor)] = true;
                break;
            }
            if (hitMirror < 0) break;
            const Mirror& m = mirrors_[size_t(hitMirror)];
            float a = float(m.angle) * kPi * 0.25f;
            Vec3 n{std::cos(a), 0, std::sin(a)};
            d = normalize(d - n * (2.0f * dot(d, n)));
            if (m.filter) color = m.filter;
            p = end;
            lastMirror = hitMirror;
        }
    }
    for (size_t i = 0; i < receptors_.size(); ++i) {
        Receptor& r = receptors_[i];
        if (lit[i] && !r.lit) {  // se enciende: abre su puerta (y queda abierta)
            r.lit = true;
            r.litTime = 0;
            setGate(r.gate, false);
            spawnSparks(r.pos, {0, 1, 0}, 30, r.needColor ? r.needColor : rgba(255, 245, 220), 4.0f);
            uiSound(Sfx::Chest, 0.8f);
            croma_ = std::min(1.0f, croma_ + 0.05f);
            playScene(r.scene);
        }
    }
}
