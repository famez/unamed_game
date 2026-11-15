// Modo prueba (uno solo, para probarlo todo): sin historia, en un patio generado al azar
// (edificios alrededor, pilares, plataformas, árboles, farolas, nodos encendidos y dos
// portales) con Grises de luz y de sombra sin fin, cada vez más numerosos; cada 30 bajas
// aparece un Gris mayor. Entre las farolas queda oscuridad de sobra para la sombra.
// Vida y Poder infinitos se encienden y apagan aparte (F1, F2): por defecto solo la vida,
// para probar la gestión del Poder (gastarlo, recogerlo, romper farolas, interruptores).
// Todo sale de una semilla: la misma semilla da el mismo escenario.

#include "game/game.h"

#include <algorithm>

namespace {

const uint32_t kWhite = rgba(150, 150, 154), kCyan = rgba(40, 220, 255), kMagenta = rgba(255, 70, 190);
const uint32_t kAmber = rgba(255, 190, 60), kLime = rgba(120, 255, 140), kViolet = rgba(150, 110, 255);
const uint32_t kNeons[] = {kCyan, kMagenta, kAmber, kLime, kViolet};
const uint32_t kTints[] = {rgba(150, 150, 154), rgba(138, 142, 156), rgba(136, 152, 146), rgba(160, 140, 146),
                           rgba(164, 152, 136), rgba(146, 140, 160)};
const uint32_t kLeaves[] = {rgba(255, 110, 200), rgba(170, 110, 255), rgba(90, 210, 255), rgba(255, 150, 90),
                            rgba(110, 230, 150)};

}  // namespace

void Game::initTest(TestMode mode, uint32_t seed) {
    init();  // sin guion: ni escenas ni combates de zona
    testMode_ = mode;
    testSeed_ = seed;
    rng_ = seed * 2654435761u + 0x9E3779B9u;
    loadLevel(LevelId::Prueba);
    // Todo a mano para probar: armas, núcleos de las formas y el farolillo de Nora.
    for (ItemId w : {ItemId::StarStaff, ItemId::AuroraBlade, ItemId::LightLance, ItemId::LightFists, ItemId::ShadowClaws,
                     ItemId::EclipseScythe})
        inv_.add(w);
    inv_.add(ItemId::NoraLantern);
    inv_.add(ItemId::Potion, 9);
    // Y materiales de sobra para forjar espadas en el taller (el nivel sí hay que subirlo).
    for (ItemId m : {ItemId::ShinyShard, ItemId::GoldSpore, ItemId::WingDust}) inv_.add(m, 40);
    inv_.add(ItemId::BlazeGem, 2);
    hp_ = hpGhost_ = maxHp_;
    mp_ = maxMp_;
    shadowUnlocked_ = true;
    applyWeapon();
    notify("Modo prueba · F1 vida y F2 Poder infinitos · Z cambia de modo", kCyan);
}

void Game::initLevel(LevelId id) {
    init();  // sin guion
    freePlay_ = true;
    loadLevel(id);  // (otra vez si es la plaza: ahora con los combates armados)
    hp_ = hpGhost_ = maxHp_;
    mp_ = maxMp_;
    shadowUnlocked_ = true;
    applyWeapon();
    notify("Z cambia entre luz y sombra · F1 vida y F2 Poder infinitos", kCyan);
}

void Game::setTestCheats(bool hp, bool power) {
    infHp_ = hp;
    infPower_ = power;
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
    // Farolas (la red de luz; entre ellas, la oscuridad) y nodos ya encendidos.
    int lamps = 6 + int(random01() * 4);
    for (int i = 0; i < lamps; ++i)
        if (findSpot(0.6f, p)) addLampPost(p, kNeons[(theme + i) % 5]);
    // Dos portales para cambiar de luz a sombra.
    for (int i = 0; i < 2; ++i)
        if (findSpot(1.5f, p)) addPortal(p);
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
    // Siempre hay Grises: más cuantos más se disipan (de 3 a 9 a la vez).
    int alive = 0;
    for (const Enemy& e : enemies_) alive += e.alive();
    int want = std::min(9, 3 + kills_ / 8);
    testSpawnTimer_ -= dt;
    if (!devNoSpawns_ && alive < want && testSpawnTimer_ <= 0 && hero_.downTimer < 0 && spawnTestEnemy()) testSpawnTimer_ = 0.8f;
    if (!parts_.empty()) {
        auto mark = [](bool on) { return on ? "infinita" : "no"; };
        parts_[0].objective = "Grises: " + std::to_string(kills_) + " · F1 vida " + mark(infHp_) + " · F2 Poder " + mark(infPower_);
    }
}

// --- Ayudas para los guiones de grabación -----------------------------------------------------

void Game::devPlace(Vec3 pos, float yaw) {
    hero_.teleport(pos, yaw);
    lock_ = -1;
}

void Game::devClearEnemies() {
    enemies_.clear();
    projectiles_.clear();
    lock_ = -1;
}

void Game::devSpawn(EnemyKind kind, Vec3 pos) {
    Vec3 p = pos;
    if (devFreeSpot(p, 0.8f)) spawnEnemy({kind, p});
}

bool Game::devFreeSpot(Vec3& p, float radius) const {
    auto free = [&](Vec3 q) {
        if (testMode_ != TestMode::None && (std::fabs(q.x) > testArena_ - 1.5f || std::fabs(q.z) > testArena_ - 1.5f))
            return false;
        for (const AABB& s : world_.solids)
            if (q.x > s.min.x - radius && q.x < s.max.x + radius && q.z > s.min.z - radius && q.z < s.max.z + radius &&
                s.max.y > 0.3f)
                return false;
        return true;
    };
    for (int ring = 0; ring < 30; ++ring)
        for (int k = 0; k < 12; ++k) {
            float a = float(k) / 12.0f * 2.0f * kPi, d = 0.5f * float(ring);
            Vec3 q = p + Vec3{std::cos(a) * d, 0, std::sin(a) * d};
            q.y = 0;
            if (free(q)) {
                p = q;
                return true;
            }
            if (ring == 0) break;
        }
    return false;
}
