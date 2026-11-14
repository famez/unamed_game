// Modos de prueba: sin historia y con vida y Luz infinitas.
//   - Combate: un patio generado al azar (edificios alrededor, pilares, plataformas, árboles,
//     farolas y nodos encendidos) con Grises sin fin, cada vez más numerosos; cada 30 bajas
//     aparece un Gris mayor.
//   - Viaje por la luz: un archipiélago de islas flotantes sobre el vacío, cada una con su
//     baliza; casi ninguna se alcanza saltando, hay que viajar de luz en luz.
// Todo sale de una semilla: la misma semilla da el mismo escenario.

#include "game.h"

#include <algorithm>

namespace {

const uint32_t kWhite = rgba(150, 150, 154), kCyan = rgba(40, 220, 255), kMagenta = rgba(255, 70, 190);
const uint32_t kAmber = rgba(255, 190, 60), kLime = rgba(120, 255, 140), kViolet = rgba(150, 110, 255);
const uint32_t kNeons[] = {kCyan, kMagenta, kAmber, kLime, kViolet};
const uint32_t kTints[] = {rgba(150, 150, 154), rgba(138, 142, 156), rgba(136, 152, 146), rgba(160, 140, 146),
                           rgba(164, 152, 136), rgba(146, 140, 160)};
const uint32_t kLeaves[] = {rgba(255, 110, 200), rgba(170, 110, 255), rgba(90, 210, 255), rgba(255, 150, 90),
                            rgba(110, 230, 150)};

bool inside(const AABB& a, Vec3 p) {
    return p.x >= a.min.x && p.x <= a.max.x && p.y >= a.min.y && p.y <= a.max.y && p.z >= a.min.z && p.z <= a.max.z;
}

}  // namespace

void Game::initTest(TestMode mode, uint32_t seed) {
    init();  // sin guion: ni escenas ni combates de zona
    testMode_ = mode;
    testSeed_ = seed;
    rng_ = seed * 2654435761u + 0x9E3779B9u;
    loadLevel(mode == TestMode::Light ? LevelId::PruebaLuz : LevelId::Prueba);
    // Todo a mano para probar: armas, núcleos de las formas y el farolillo de Nora.
    for (ItemId w : {ItemId::StarStaff, ItemId::AuroraBlade, ItemId::LightLance, ItemId::LightFists}) inv_.add(w);
    for (ItemId c : {ItemId::CoreAurora, ItemId::CoreColossus, ItemId::CoreEcho}) inv_.add(c, 99);
    inv_.add(ItemId::NoraLantern);
    inv_.add(ItemId::Potion, 9);
    hp_ = hpGhost_ = maxHp_;
    mp_ = maxMp_;
    shadow_ = maxShadow_;
    shadowUnlocked_ = true;
    applyWeapon();
    notify(mode == TestMode::Light ? "Modo prueba · viaje por la luz" : "Modo prueba · Grises sin fin", kCyan);
}

// --- Patio de combate aleatorio -------------------------------------------------------------

void Game::buildPrueba() {
    const char* names[] = {"Patio Cian", "Patio Magenta", "Patio Ámbar", "Patio Lima", "Patio Violeta"};
    int theme = int(random01() * 5) % 5;
    levelName_ = std::string("Prueba: ") + names[theme];
    bool grass = random01() < 0.5f;
    floorColor_ = grass ? rgba(70, 120, 90) : rgba(104, 106, 112);
    floorMaterial_ = grass ? Material::Grass : Material::Ground;
    const float H = random01() < 0.5f ? 20.0f : 25.0f;
    testArena_ = H;

    // Cierre: edificios en los cuatro lados y bloques en las esquinas.
    int k = 0;
    for (float t = -H + 5; t < H; t += 10, ++k) {
        float hgt = 4.5f + random01() * 3.0f;
        addBuilding(t, H + 3, 5.0f, 3.0f, hgt, {0, 0, -1}, kTints[k % 6], kNeons[(theme + k) % 5]);
        addBuilding(t, -H - 3, 5.0f, 3.0f, 4.5f + random01() * 3.0f, {0, 0, 1}, kTints[(k + 2) % 6], kNeons[(theme + k + 1) % 5]);
        addBuilding(H + 3, t, 3.0f, 5.0f, 4.5f + random01() * 3.0f, {-1, 0, 0}, kTints[(k + 4) % 6], kNeons[(theme + k + 2) % 5]);
        addBuilding(-H - 3, t, 3.0f, 5.0f, 4.5f + random01() * 3.0f, {1, 0, 0}, kTints[(k + 1) % 6], kNeons[(theme + k + 3) % 5]);
    }
    for (float sx : {-1.0f, 1.0f})
        for (float sz : {-1.0f, 1.0f}) addBlock({sx * (H + 3), 3.5f, sz * (H + 3)}, {3, 3.5f, 3}, kTints[1]);

    // Elementos repartidos al azar, sin pisarse ni ocupar el centro (donde se aparece).
    struct Spot {
        Vec3 p;
        float r;
    };
    std::vector<Spot> used{{{0, 0, 0}, 4.5f}};
    auto findSpot = [&](float r, Vec3& out) {
        for (int tries = 0; tries < 40; ++tries) {
            Vec3 p{randRange(-H + r + 1.5f, H - r - 1.5f), 0, randRange(-H + r + 1.5f, H - r - 1.5f)};
            bool ok = true;
            for (const Spot& s : used)
                if (length(flat(p - s.p)) < s.r + r + 1.2f) ok = false;
            if (ok) {
                used.push_back({p, r});
                out = p;
                return true;
            }
        }
        return false;
    };
    Vec3 p;
    int features = 8 + int(random01() * 6);
    for (int i = 0; i < features; ++i) {
        float r = random01();
        uint32_t neon = kNeons[(theme + i) % 5];
        if (r < 0.3f && findSpot(1.2f, p)) {  // pilar
            float h = 1.0f + random01() * 3.0f, w = 0.5f + random01() * 0.6f;
            addBlock(p + Vec3{0, h, 0}, {w, h, w}, kTints[i % 6]);
            addRim(p + Vec3{0, 2 * h, 0}, w, w, neon);
        } else if (r < 0.55f && findSpot(3.5f, p)) {  // plataforma con escalón
            float w = 2.0f + random01() * 1.5f, h = 0.45f + random01() * 0.5f;
            addBlock(p + Vec3{0, h, 0}, {w, h, w}, rgba(132, 134, 140));
            addBlock(p + Vec3{0, h * 0.5f, w + 0.4f}, {1.2f, h * 0.5f, 0.4f}, rgba(132, 134, 140));
            addRim(p + Vec3{0, 2 * h, 0}, w, w, neon);
        } else if (r < 0.75f && findSpot(1.6f, p)) {
            addTree(p, 0.9f + random01() * 0.4f, kLeaves[(theme + i) % 5]);
        } else if (findSpot(1.0f, p)) {  // montón de cajas rompibles
            for (int c = 0; c < 3; ++c) {
                Vec3 o = p + Vec3{randRange(-0.6f, 0.6f), 0, randRange(-0.6f, 0.6f)} * float(c);
                crates_.push_back({boxFromCenter(o + Vec3{0, 0.4f, 0}, {0.4f, 0.4f, 0.4f})});
            }
        }
    }
    // Farolas (la red de luz) y nodos ya encendidos (zonas de luz donde absorber).
    int lamps = 6 + int(random01() * 4);
    for (int i = 0; i < lamps; ++i)
        if (findSpot(0.6f, p)) addLampPost(p, kNeons[(theme + i) % 5]);
    int nodes = 2 + int(random01() * 2);
    for (int i = 0; i < nodes; ++i)
        if (findSpot(1.0f, p)) {
            ColorNode n;
            n.id = "nodo_prueba_" + std::to_string(i);
            n.pos = p;
            n.radius = 9;
            n.color = kNeons[(theme + i * 2) % 5];
            n.lit = true;
            n.litTime = 10;
            nodes_.push_back(n);
        }

    LevelPart part;
    part.title = "Grises sin fin";
    part.objective = "Grises disipados: 0";
    part.kind = PartKind::Exit;  // nunca se termina
    part.area = AABB{{1e6f, 1e6f, 1e6f}, {1e6f, 1e6f, 1e6f}};
    parts_ = {part};
    heroStart_ = {0, 0, 0};
    heroStartYaw_ = 0;
    croma_ = cromaShown_ = 0.3f;
    testSpawnTimer_ = 1.5f;
    testBossAt_ = 30;
}

// --- Archipiélago de luz ---------------------------------------------------------------------

void Game::buildPruebaLuz() {
    levelName_ = "Prueba: Archipiélago de luz";
    world_.ground = false;
    const Mat4 id = Mat4::identity();
    islands_.clear();
    auto island = [&](Vec3 c, float hx, float hz, uint32_t rim, bool lamp) {
        addBlock(c - Vec3{0, 0.3f, 0}, {hx, 0.3f, hz}, kWhite);
        addRim(c, hx, hz, rim);
        addDecor(id, c - Vec3{0, 0.8f, 0}, {hx * 0.6f, 0.25f, hz * 0.6f}, kTints[1], Material::Stone, false);
        TestIsland t;
        t.top = AABB{{c.x - hx, c.y - 0.2f, c.z - hz}, {c.x + hx, c.y + 1.5f, c.z + hz}};
        if (lamp) {
            Vec3 base = c + Vec3{randRange(-hx, hx) * 0.6f, 0, randRange(-hz, hz) * 0.6f};
            addLampPost(base, rim);
            t.lamp = lamps_.back().pos;
        }
        islands_.push_back(t);
    };
    island({0, 0, 0}, 5, 5, kCyan, true);  // la de salida
    islands_[0].visited = true;
    // Cada isla nueva sale de una anterior a 12–28 m (dentro del alcance del viaje).
    int count = 14 + int(random01() * 6);
    for (int i = 0, tries = 0; i < count && tries < 600; ++tries) {
        const TestIsland& from = islands_[size_t(random01() * float(islands_.size())) % islands_.size()];
        Vec3 fc = (from.top.min + from.top.max) * 0.5f;
        float a = random01() * 2.0f * kPi, d = randRange(12, 28);
        float hx = randRange(1.5f, 4.0f), hz = randRange(1.5f, 4.0f);
        Vec3 c{fc.x + std::cos(a) * d, clampf(from.top.min.y + 0.2f + randRange(-3, 6), -2, 22), fc.z + std::sin(a) * d};
        bool ok = true;
        for (const TestIsland& t : islands_) {
            Vec3 tc = (t.top.min + t.top.max) * 0.5f;
            if (length(flat(c - tc)) < std::max(hx, hz) + std::max(t.top.max.x - tc.x, t.top.max.z - tc.z) + 4.0f) ok = false;
        }
        if (!ok) continue;
        island(c, hx, hz, kNeons[i % 5], true);
        ++i;
    }
    // Agujas altas con una luz arriba: solo se sube viajando.
    for (int i = 0; i < 4; ++i) {
        const TestIsland& t = islands_[1 + size_t(random01() * float(islands_.size() - 1)) % (islands_.size() - 1)];
        Vec3 corner{t.top.max.x - 0.5f, t.top.min.y + 0.2f, t.top.min.z + 0.5f};
        float h = randRange(4, 8);
        addBlock(corner + Vec3{0, h * 0.5f, 0}, {0.35f, h * 0.5f, 0.35f}, kWhite);
        lamps_.push_back({corner + Vec3{0, h + 0.3f, 0}, kMagenta});
    }

    LevelPart part;
    part.title = "De luz en luz";
    part.kind = PartKind::Exit;
    part.area = AABB{{1e6f, 1e6f, 1e6f}, {1e6f, 1e6f, 1e6f}};
    parts_ = {part};
    heroStart_ = islands_[0].lamp - Vec3{0, 3.35f, 1.6f};  // junto a la primera baliza
    heroStartYaw_ = 0;
    croma_ = cromaShown_ = 0.4f;
}

// --- Lógica de los modos de prueba -----------------------------------------------------------

bool Game::spawnTestEnemy() {
    for (int tries = 0; tries < 20; ++tries) {
        float a = random01() * 2.0f * kPi, d = randRange(9, 18);
        Vec3 p = hero_.body.pos + Vec3{std::cos(a) * d, 0, std::sin(a) * d};
        if (std::fabs(p.x) > testArena_ - 1.5f || std::fabs(p.z) > testArena_ - 1.5f) continue;
        p.y = 0;
        bool free = true;
        for (const AABB& s : world_.solids)
            if (p.x > s.min.x - 1 && p.x < s.max.x + 1 && p.z > s.min.z - 1 && p.z < s.max.z + 1) free = false;
        if (!free) continue;
        Spawn s;
        float r = random01();
        s.kind = r < 0.3f ? EnemyKind::Dazzled : r < 0.5f ? EnemyKind::Watcher : r < 0.8f ? EnemyKind::Hound : EnemyKind::Brute;
        s.pos = p;
        bool bossAlive = false;
        for (const Enemy& e : enemies_) bossAlive |= e.boss && e.alive();
        if (kills_ >= testBossAt_ && !bossAlive) {  // Gris mayor
            s.kind = EnemyKind::Brute;
            s.boss = true;
            s.name = "Gris mayor";
            testBossAt_ += 30;
            notify("Se acerca un Gris mayor", rgba(255, 140, 160));
        }
        spawnEnemy(s);
        return true;
    }
    return false;
}

void Game::updateTestMode(float dt) {
    if (testMode_ == TestMode::None) return;
    if (testMode_ == TestMode::Combat) {
        // Siempre hay Grises: más cuantos más se disipan (de 3 a 9 a la vez).
        int alive = 0;
        for (const Enemy& e : enemies_) alive += e.alive();
        int want = std::min(9, 3 + kills_ / 8);
        testSpawnTimer_ -= dt;
        if (alive < want && testSpawnTimer_ <= 0 && hero_.downTimer < 0 && spawnTestEnemy()) testSpawnTimer_ = 0.8f;
        if (!parts_.empty()) parts_[0].objective = "Grises disipados: " + std::to_string(kills_);
        return;
    }
    // Archipiélago: al posarse en una isla nueva se marca y pasa a ser el punto de control.
    int visited = 0;
    for (TestIsland& t : islands_) {
        bool here = (hero_.body.grounded && !travel_ && inside(t.top, hero_.body.pos)) ||
                    (inLight_ && length(hostPos_ - t.lamp) < 0.1f);
        if (!t.visited && here) {
            t.visited = true;
            spawnRing(hero_.body.pos + Vec3{0, 0.05f, 0}, 2.0f, 0.6f, kAmber);
            uiSound(Sfx::ItemGet, 0.6f, 1.2f);
        }
        visited += t.visited;
    }
    if (hero_.body.grounded && !travel_ && !inLight_ && hero_.downTimer < 0)
        for (const TestIsland& t : islands_)
            if (inside(t.top, hero_.body.pos)) checkpoint_ = hero_.body.pos;
    if (!parts_.empty()) {
        LevelPart& p = parts_[0];
        p.objective = "Islas alcanzadas: " + std::to_string(visited) + "/" + std::to_string(islands_.size());
        // El indicador señala la baliza sin visitar más cercana.
        float best = 1e9f;
        for (const TestIsland& t : islands_)
            if (!t.visited && length(t.lamp - hero_.body.pos) < best) best = length(t.lamp - hero_.body.pos), p.marker = t.lamp;
        if (best > 1e8f) p.marker = hero_.body.pos + Vec3{0, 3, 0};
    }
}
