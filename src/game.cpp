#include "game.h"

#include <algorithm>

namespace {

// Golpes del protagonista: multiplicador del daño del arma, alcance extra, arco (coseno
// del medio ángulo; -1 = alrededor) y empuje horizontal / vertical (m/s).
struct HitParams {
    float damage, extraRange, cosArc, knock, lift;
    bool heavy;  // atraviesa la "armadura" de los enemigos que están atacando
};
// clang-format off
const HitParams kHits[int(Move::Count)] = {
    {0,    0,     1,     0,    0,    false},  // None
    {1.0f, 0,     0.1f,  3.0f, 0.8f, false},  // Slash1
    {1.0f, 0,     0.1f,  3.0f, 0.8f, false},  // Slash2
    {1.2f, 0.2f, -1.0f,  4.5f, 1.5f, false},  // Slash3: giro, golpea alrededor
    {2.2f, 0.6f, -1.0f,  9.0f, 5.5f, true},   // Finisher: lanza por los aires
    {1.0f, 0.1f,  0.0f,  1.5f, 2.5f, false},  // Air1: mantiene al enemigo en el aire
    {1.0f, 0.1f,  0.0f,  1.5f, 2.5f, false},  // Air2
    {2.0f, 0.3f, -1.0f,  8.0f, 3.0f, true},   // AirFinisher
    {0,    0,     1,     0,    0,    false},  // Cast
};
const SpellDef kSpells[int(Spell::Count)] = {
    {"Fuego", 8, rgba(255, 140, 50)},
    {"Hielo", 10, rgba(120, 220, 255)},
    {"Cura", 25, rgba(120, 255, 150)},
};
// Oleadas: duendes, setas, polillas. Después de la última se repite con uno más de cada.
const int kWaves[][3] = {{3, 0, 0}, {2, 1, 0}, {3, 0, 2}, {2, 2, 2}, {4, 2, 3}};
constexpr int kWaveCount = int(sizeof(kWaves) / sizeof(kWaves[0]));
// clang-format on

constexpr float kHeroRespawn = 3.0f;
constexpr float kInvulnAfterHit = 0.8f;
constexpr float kPerfectGuard = 0.35f;  // bloquear al poco de levantar la guardia rechaza al atacante
constexpr float kMpRegen = 1.0f;        // PM por segundo
constexpr float kHpOrb = 8, kMpOrb = 6;
constexpr float kArena = 15.4f;         // los enemigos no salen de |x|, |z| < esto
constexpr size_t kMaxParticles = 900;

uint32_t scaleColor(uint32_t c, float k) {
    auto ch = [&](int shift) { return uint32_t(clampf(float((c >> shift) & 0xFF) * k, 0, 255)) << shift; };
    return ch(0) | ch(8) | ch(16) | (c & 0xFF000000u);
}

uint32_t enemyColor(EnemyKind k) {
    switch (k) {
        case EnemyKind::Imp: return rgba(220, 150, 255);
        case EnemyKind::Shroom: return rgba(90, 240, 220);
        default: return rgba(255, 190, 235);
    }
}

}  // namespace

const SpellDef& spellDef(Spell s) { return kSpells[int(s)]; }

float Game::random01() {
    rng_ = rng_ * 1664525u + 1013904223u;
    return float(rng_ >> 8) / float(1u << 24);
}

// --- Escenario --------------------------------------------------------------------------

void Game::buildLevel() {
    auto block = [&](Vec3 c, Vec3 h, uint32_t col, Material m = Material::Stone) {
        blocks_.push_back({boxFromCenter(c, h), col, m});
    };
    auto deco = [&](const Mat4& t, Vec3 c, Vec3 h, uint32_t col, Material m, bool sphere) {
        decor_.push_back({t, c, h, col, m, sphere});
    };
    const Mat4 id = Mat4::identity();
    // Paleta: arquitectura blanca y nacarada; el color lo ponen los neones, las plantas y
    // los hologramas.
    const uint32_t white = rgba(226, 230, 240), glass = rgba(40, 92, 130), cyan = rgba(40, 220, 255);
    const uint32_t magenta = rgba(255, 70, 190), amber = rgba(255, 190, 60), lime = rgba(120, 255, 140);
    const uint32_t neons[] = {cyan, magenta, amber, lime};
    // Contorno luminoso de la cara superior de un bloque (cuatro tiras finas).
    auto rim = [&](Vec3 c, float hx, float hz, uint32_t col) {
        const float t = 0.025f;
        deco(id, c + Vec3{0, 0, hz}, {hx + t, t, t}, col, Material::Glow, false);
        deco(id, c - Vec3{0, 0, hz}, {hx + t, t, t}, col, Material::Glow, false);
        deco(id, c + Vec3{hx, 0, 0}, {t, t, hz + t}, col, Material::Glow, false);
        deco(id, c - Vec3{hx, 0, 0}, {t, t, hz + t}, col, Material::Glow, false);
    };

    // Edificio: bloque blanco con cornisa, franja de neón en lo alto, cristaleras con marco
    // luminoso, puerta en arco de luz y un cartel holográfico (`in` apunta hacia la plaza).
    auto building = [&](float cx, float cz, float hx, float hz, float height, Vec3 in, uint32_t tint, uint32_t neon) {
        block({cx, height * 0.5f, cz}, {hx, height * 0.5f, hz}, tint);
        deco(id, {cx, height + 0.12f, cz}, {hx + 0.15f, 0.12f, hz + 0.15f}, white, Material::Stone, false);  // cornisa
        deco(id, {cx, height + 0.02f, cz}, {hx + 0.16f, 0.035f, hz + 0.16f}, neon, Material::Glow, false);
        bool longX = hx > hz;
        float shortHalf = longX ? hz : hx, longHalf = longX ? hx : hz;
        Vec3 along = longX ? Vec3{1, 0, 0} : Vec3{0, 0, 1};
        Vec3 face = Vec3{cx, 0, cz} + in * (shortHalf + 0.03f);
        auto panel = [&](Vec3 c, float w, float h, uint32_t col, Material m, float depth = 0.05f) {
            deco(id, c, longX ? Vec3{w, h, depth} : Vec3{depth, h, w}, col, m, false);
        };
        // Cúpula o antena en el tejado.
        if (int(cx * 3 + cz) % 2 == 0)
            deco(id, {cx + along.x * longHalf * 0.4f, height + 0.25f, cz + along.z * longHalf * 0.4f}, {1.2f, 0.9f, 1.2f},
                 white, Material::Stone, true);
        else {
            Vec3 a{cx - along.x * longHalf * 0.5f, height, cz - along.z * longHalf * 0.5f};
            deco(id, a + Vec3{0, 1.0f, 0}, {0.06f, 1.0f, 0.06f}, white, Material::Metal, false);
            deco(id, a + Vec3{0, 2.05f, 0}, {0.12f, 0.12f, 0.12f}, neon, Material::Glow, true);
        }
        // Puerta: arco de luz.
        panel(face + Vec3{0, 1.15f, 0}, 0.7f, 1.15f, rgba(30, 40, 70), Material::Metal);
        panel(face + Vec3{0, 2.32f, 0} + in * 0.01f, 0.78f, 0.04f, neon, Material::Glow);
        panel(face + Vec3{0, 1.15f, 0} + along * 0.74f + in * 0.01f, 0.04f, 1.15f, neon, Material::Glow);
        panel(face + Vec3{0, 1.15f, 0} - along * 0.74f + in * 0.01f, 0.04f, 1.15f, neon, Material::Glow);
        // Cartel holográfico encima de la puerta.
        panel(face + Vec3{0, 3.0f, 0} + in * 0.35f, 0.9f, 0.28f, neons[int(cx + cz + 8) & 3], Material::Glow, 0.015f);
        for (float side : {-1.0f, 1.0f}) {
            for (float y : {1.7f, height > 5.0f ? 4.0f : -1.0f}) {
                if (y < 0) continue;
                Vec3 w = face + along * (side * longHalf * 0.55f) + Vec3{0, y, 0};
                panel(w, 0.9f, 0.7f, glass, Material::Metal);
                panel(w + Vec3{0, -0.72f, 0} + in * 0.02f, 0.9f, 0.025f, neon, Material::Glow);  // repisa luminosa
            }
            // Jardinera blanca con plantas y flores de colores.
            Vec3 box = face + along * (side * longHalf * 0.55f) + in * 0.3f + Vec3{0, 0.3f, 0};
            deco(id, box, longX ? Vec3{0.9f, 0.3f, 0.25f} : Vec3{0.25f, 0.3f, 0.9f}, white, Material::Stone, false);
            for (int i = 0; i < 5; ++i) {
                Vec3 p = box + along * (-0.68f + 0.34f * float(i)) + Vec3{0, 0.36f, 0};
                deco(id, p, {0.16f, 0.13f, 0.16f}, rgba(70, 190, 120), Material::Cloth, true);
                deco(id, p + Vec3{0, 0.12f, 0}, {0.06f, 0.05f, 0.06f}, neons[(i + int(cx)) & 3], Material::Glow, true);
            }
        }
    };
    const uint32_t tints[] = {rgba(226, 230, 240), rgba(205, 212, 250), rgba(200, 238, 222), rgba(250, 210, 228),
                              rgba(250, 226, 196), rgba(222, 208, 250)};
    int k = 0;
    for (float x : {-11.0f, 0.0f, 11.0f}) {
        building(x, 19, 5.0f, 3.0f, 4.6f + float(k % 3) * 1.0f, {0, 0, -1}, tints[k % 6], neons[k & 3]); ++k;
        building(x, -19, 5.0f, 3.0f, 4.6f + float(k % 3) * 1.0f, {0, 0, 1}, tints[k % 6], neons[k & 3]); ++k;
        building(19, x, 3.0f, 5.0f, 4.6f + float(k % 3) * 1.0f, {-1, 0, 0}, tints[k % 6], neons[k & 3]); ++k;
        building(-19, x, 3.0f, 5.0f, 4.6f + float(k % 3) * 1.0f, {1, 0, 0}, tints[k % 6], neons[k & 3]); ++k;
    }
    // Torres en las esquinas con anillos de luz y faro arriba; entre edificios, jardineras
    // altas: la plaza queda cerrada.
    for (float sx : {-1.0f, 1.0f})
        for (float sz : {-1.0f, 1.0f}) {
            Vec3 c{sx * 18.5f, 0, sz * 18.5f};
            block(c + Vec3{0, 4.5f, 0}, {2.6f, 4.5f, 2.6f}, white);
            for (int r = 0; r < 3; ++r)
                deco(id, c + Vec3{0, 2.5f + 2.5f * float(r), 0}, {2.68f, 0.06f, 2.68f}, neons[(r + int(sx + 2 * sz + 3)) & 3],
                     Material::Glow, false);
            deco(id, c + Vec3{0, 9.0f, 0}, {2.4f, 1.4f, 2.4f}, white, Material::Stone, true);
            deco(id, c + Vec3{0, 10.6f, 0}, {0.3f, 0.3f, 0.3f}, cyan, Material::Glow, true);
        }
    for (float g : {-5.5f, 5.5f}) {
        for (Vec3 c : {Vec3{g, 1.0f, 17.0f}, Vec3{g, 1.0f, -17.0f}, Vec3{17.0f, 1.0f, g}, Vec3{-17.0f, 1.0f, g}}) {
            Vec3 half{std::fabs(c.x) > 16 ? 1.3f : 0.6f, 1.0f, std::fabs(c.z) > 16 ? 1.3f : 0.6f};
            block(c, half, white);
            deco(id, c + Vec3{0, 1.0f, 0}, half + Vec3{0.02f, 0.04f, 0.02f}, magenta, Material::Glow, false);
            deco(id, c + Vec3{0, 1.35f, 0}, {half.x * 1.1f, 0.45f, half.z * 1.1f}, rgba(70, 200, 120), Material::Cloth, true);
        }
    }

    // Fuente de luz: pilón blanco (hay que saltar), agua luminosa, columna con espiral de
    // neón y un prisma que flota y gira encima (se dibuja aparte, ver drawLevel).
    block({0, 0.35f, 0}, {2.4f, 0.35f, 2.4f}, white);
    block({0, 1.0f, 0}, {0.4f, 1.0f, 0.4f}, white);
    block({0, 2.05f, 0}, {1.0f, 0.12f, 1.0f}, white);
    deco(id, {0, 0.66f, 0}, {2.15f, 0.05f, 2.15f}, rgba(40, 125, 185), Material::Water, false);
    deco(id, {0, 2.19f, 0}, {0.88f, 0.03f, 0.88f}, rgba(40, 125, 185), Material::Water, false);
    rim({0, 0.70f, 0}, 2.4f, 2.4f, cyan);  // borde del pilón
    for (int i = 0; i < 10; ++i) {
        float a = float(i) * 0.9f, y = 0.2f + 0.18f * float(i);
        deco(translate({0, y, 0}) * rotateY(a), {0.41f, 0, 0}, {0.02f, 0.05f, 0.12f}, magenta, Material::Glow, false);
    }
    lamps_.push_back({0, 3.0f, 0});

    // Terraza al noreste con dos escalones (se suben andando) y un árbol.
    block({10.5f, 0.45f, 10.5f}, {3.5f, 0.45f, 3.5f}, rgba(214, 222, 240));
    block({10.5f, 0.15f, 6.1f}, {1.5f, 0.15f, 0.3f}, rgba(214, 222, 240));
    block({10.5f, 0.30f, 6.7f}, {1.5f, 0.30f, 0.3f}, rgba(214, 222, 240));
    rim({10.5f, 0.9f, 10.5f}, 3.5f, 3.5f, cyan);
    // Plataforma alta al noroeste: se llega saltando por dos bloques; bajos luminosos.
    block({-6.5f, 0.45f, 7.0f}, {0.8f, 0.45f, 0.8f}, white);
    block({-8.7f, 1.6f, 9.0f}, {0.8f, 0.15f, 0.8f}, white);
    block({-11.5f, 2.4f, 11.5f}, {2.2f, 0.2f, 2.2f}, white);
    block({-11.5f, 1.1f, 11.5f}, {0.35f, 1.1f, 0.35f}, white);
    rim({-8.7f, 1.75f, 9.0f}, 0.8f, 0.8f, magenta);
    rim({-11.5f, 2.6f, 11.5f}, 2.2f, 2.2f, magenta);
    rim({-6.5f, 0.9f, 7.0f}, 0.8f, 0.8f, magenta);

    // Árboles: tronco blanco y copa de colores vivos con frutos de luz.
    auto tree = [&](Vec3 base, float size, uint32_t leaves) {
        block(base + Vec3{0, 1.1f * size, 0}, {0.2f * size, 1.1f * size, 0.2f * size}, white, Material::Stone);
        Vec3 top = base + Vec3{0, 2.5f * size, 0};
        deco(id, top, Vec3{1.3f, 1.0f, 1.3f} * size, leaves, Material::Cloth, true);
        deco(id, top + Vec3{0.5f, 0.6f, 0.2f} * size, Vec3{0.9f, 0.75f, 0.9f} * size, scaleColor(leaves, 1.12f), Material::Cloth, true);
        deco(id, top + Vec3{-0.45f, 0.4f, -0.4f} * size, Vec3{0.9f, 0.75f, 0.9f} * size, scaleColor(leaves, 0.88f), Material::Cloth, true);
        for (int i = 0; i < 5; ++i) {
            float a = float(i) * 1.26f;
            deco(id, top + Vec3{std::cos(a) * 1.1f, -0.45f + 0.2f * float(i % 2), std::sin(a) * 1.1f} * size,
                 {0.08f, 0.08f, 0.08f}, rgba(255, 245, 160), Material::Glow, true);
        }
    };
    tree({12.0f, 0.9f, 12.5f}, 1.0f, rgba(255, 110, 200));
    tree({-13.0f, 0, -4.0f}, 1.2f, rgba(170, 110, 255));
    tree({13.0f, 0, -5.0f}, 1.1f, rgba(255, 120, 170));
    tree({-4.0f, 0, -13.5f}, 1.0f, rgba(90, 210, 255));
    tree({4.5f, 0, 13.5f}, 1.1f, rgba(255, 150, 90));

    // Farolas: poste blanco fino y esfera de luz.
    for (Vec3 p : {Vec3{5.5f, 0, -5.5f}, Vec3{-5.5f, 0, -5.5f}, Vec3{5.5f, 0, 5.0f}, Vec3{-4.0f, 0, 4.0f}}) {
        block(p + Vec3{0, 1.6f, 0}, {0.08f, 1.6f, 0.08f}, white, Material::Metal);
        deco(id, p + Vec3{0, 3.35f, 0}, {0.2f, 0.2f, 0.2f}, cyan, Material::Glow, true);
        deco(id, p + Vec3{0, 3.1f, 0}, {0.16f, 0.03f, 0.16f}, white, Material::Metal, false);
        lamps_.push_back(p + Vec3{0, 3.35f, 0});
    }

    // Cajas rompibles y cofres.
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
    c.pos = {-11.5f, 2.6f, 11.8f};
    c.yaw = kPi * 0.75f;
    c.contents = {{ItemId::AuroraBlade, 1}, {ItemId::BlazeGem, 1}, {ItemId::Elixir, 1}};
    c.munny = 50;
    chests_ = {a, b, c};

    spawnPoints_ = {{8, 0, -3}, {-8, 0, -3}, {3, 0, -9}, {-3, 0, 8}, {-8, 0, 3}, {0, 0, -12},
                    {12, 0, 0}, {-12, 0, 0}, {7, 0, 4}, {-6, 0, -9}};
    heroStart_ = {0, 0, -8};
}

void Game::rebuildSolids() {
    world_.solids.clear();
    for (const Block& b : blocks_) world_.solids.push_back(b.box);
    for (const Crate& c : crates_)
        if (!c.broken) world_.solids.push_back(c.box);
    for (const TreasureChest& c : chests_) world_.solids.push_back(boxFromCenter(c.pos + Vec3{0, 0.35f, 0}, {0.45f, 0.35f, 0.45f}));
}

void Game::init() {
    buildLevel();
    rebuildSolids();
    hero_.body.pos = heroStart_;
    inv_.add(ItemId::WoodenSword);
    inv_.add(ItemId::Potion, 3);
    inv_.add(ItemId::Ether, 1);
    applyWeapon();
}

void Game::applyWeapon() {
    const WeaponStats& w = inv_.weaponStats();
    hero_.comboHits = w.comboHits;
    hero_.attackSpeed = w.speed;
}

// --- Bucle principal ---------------------------------------------------------------------

void Game::update(float dt, const Controls& c) {
    time_ += dt;
    heroFlash_ = std::max(0.0f, heroFlash_ - dt);
    guardFlash_ = std::max(0.0f, guardFlash_ - dt);
    heroInvuln_ = std::max(0.0f, heroInvuln_ - dt);
    shake_ = std::max(0.0f, shake_ - dt * 1.5f);
    fovKick_ += ((hero_.dashAmount() > 0 ? 0.08f : 0.0f) + (isFinisher(hero_.move()) ? 0.06f : 0.0f) - fovKick_) *
                (1.0f - std::exp(-8.0f * dt));
    waveBanner_ = std::max(0.0f, waveBanner_ - dt);
    lastHitAge_ += dt;
    hpGhost_ = hpGhost_ > hp_ ? (heroFlash_ > 0 ? hpGhost_ : moveTowards(hpGhost_, hp_, 40.0f * dt)) : hp_;

    bool down = hero_.downTimer >= 0;
    if (down) {
        hero_.downTimer += dt;
        if (hero_.downTimer > kHeroRespawn) respawnHero();
    } else {
        mp_ = std::min(maxMp_, mp_ + kMpRegen * dt);
        if (c.lockOn) cycleLock(c.camForward);
        if (c.unlock) lock_ = -1;
        if (c.commandScroll != 0)
            commandSel_ = ((commandSel_ + c.commandScroll) % kCommandCount + kCommandCount) % kCommandCount;
        if (c.command >= 0 && c.command < kCommandCount) runCommand(c.command);
        else if (c.runCommand) runCommand(commandSel_);
        hero_.setGuard(c.guard);
        if (c.dodge) dodgeBuffer_ = 0.15f;
        if (c.attack) attackBuffer_ = 0.25f;
        if (c.dodge || c.attack) bufferedMove_ = c.move;
        if (dodgeBuffer_ > 0 && hero_.dodge(length(bufferedMove_) > 0.1f ? bufferedMove_ : hero_.forward())) {
            dodgeBuffer_ = 0;
            attackBuffer_ = 0;
        }
        if (attackBuffer_ > 0 && heroAttack(length(c.move) > 0.1f ? c.move : bufferedMove_)) attackBuffer_ = 0;
        attackBuffer_ = std::max(0.0f, attackBuffer_ - dt);
        dodgeBuffer_ = std::max(0.0f, dodgeBuffer_ - dt);
        if (c.interact) tryOpenChest();
    }
    if (lock_ >= int(enemies_.size()) || (lock_ >= 0 && !enemies_[size_t(lock_)].targetable())) lock_ = -1;

    // Parón al golpear: todo se congela un instante salvo los efectos.
    if (hitStop_ > 0) {
        hitStop_ -= dt;
        updateEffects(dt);
        return;
    }

    hero_.update(dt, down ? Vec3{} : c.move, !down && c.jump, world_);
    Move impact = hero_.takeImpact();
    if (impact != Move::None) resolveImpact(impact);

    // Estela del arma mientras golpea.
    for (TrailSample& t : trail_) t.age += dt;
    trail_.erase(std::remove_if(trail_.begin(), trail_.end(), [](const TrailSample& t) { return t.age > kTrailLife; }),
                 trail_.end());
    if (isAttack(hero_.move())) {
        float len = inv_.weaponStats().length;
        const Mat4& w = hero_.joints[WristR];
        trail_.push_back({transformPoint(w, kBladeBase + Vec3{0, 0, len * 0.3f}), bladeTip(), 0});
    }

    // Los enemigos de tierra son sólidos para el jugador (y entre ellos).
    for (Enemy& e : enemies_) {
        if (!e.targetable() || e.def().flying) continue;
        Body& h = hero_.body;
        bool overlapY = h.pos.y < e.body.pos.y + e.body.height && h.pos.y + h.height > e.body.pos.y;
        Vec3 d = flat(h.pos - e.body.pos);
        float dist = length(d), minDist = h.radius + e.body.radius;
        if (overlapY && dist < minDist) {
            Vec3 n = dist > 1e-4f ? d * (1.0f / dist) : Vec3{1, 0, 0};
            h.pos += n * ((minDist - dist) * 0.7f);
            e.body.pos -= n * ((minDist - dist) * 0.3f);
        }
    }
    for (size_t i = 0; i < enemies_.size(); ++i)
        for (size_t j = i + 1; j < enemies_.size(); ++j) {
            Enemy &a = enemies_[i], &b = enemies_[j];
            if (!a.alive() || !b.alive() || a.def().flying != b.def().flying) continue;
            Vec3 d = flat(a.body.pos - b.body.pos);
            float dist = length(d), minDist = a.body.radius + b.body.radius;
            if (dist >= minDist || std::fabs(a.body.pos.y - b.body.pos.y) > 1.0f) continue;
            Vec3 n = dist > 1e-4f ? d * (1.0f / dist) : Vec3{1, 0, 0};
            a.body.pos += n * ((minDist - dist) * 0.5f);
            b.body.pos -= n * ((minDist - dist) * 0.5f);
        }

    for (Enemy& e : enemies_) updateEnemy(e, dt);
    // Los que han terminado de desvanecerse se quitan (manteniendo el índice del fijado).
    {
        size_t out = 0;
        int newLock = -1;
        for (size_t i = 0; i < enemies_.size(); ++i) {
            if (enemies_[i].state == AiState::Dying && enemies_[i].stateTime > 0.5f) continue;
            if (int(i) == lock_) newLock = int(out);
            if (out != i) enemies_[out] = enemies_[i];
            ++out;
        }
        enemies_.resize(out);
        lock_ = newLock;
    }

    updateProjectiles(dt);
    updatePickups(dt);
    for (Crate& cr : crates_) cr.hitFlash = std::max(0.0f, cr.hitFlash - dt);
    for (TreasureChest& ch : chests_)
        if (ch.open) ch.openTime += dt;

    if (enemies_.empty()) {
        waveTimer_ -= dt;
        if (waveTimer_ <= 0) {
            spawnWave();
            waveTimer_ = 4.0f;
        }
    }
    updateEffects(dt);
}

void Game::spawnWave() {
    const int* w = kWaves[std::min(wave_, kWaveCount - 1)];
    int extra = std::max(0, wave_ - (kWaveCount - 1));
    ++wave_;
    waveBanner_ = 2.5f;
    for (int kind = 0; kind < int(EnemyKind::Count); ++kind)
        for (int n = 0; n < w[kind] + extra; ++n) {
            // Un punto de aparición lejos del jugador (y no siempre el mismo).
            Vec3 p = spawnPoints_[0];
            for (int tries = 0; tries < 12; ++tries) {
                p = spawnPoints_[size_t(random01() * float(spawnPoints_.size())) % spawnPoints_.size()];
                p += Vec3{randRange(-1.2f, 1.2f), 0, randRange(-1.2f, 1.2f)};
                if (length(flat(p - hero_.body.pos)) > 6.0f) break;
            }
            Vec3 to = hero_.body.pos - p;
            Enemy e = makeEnemy(EnemyKind(kind), p, std::atan2(to.x, to.z), random01() * 10.0f);
            if (e.def().flying) e.body.pos.y = e.def().hoverHeight - e.def().height * 0.5f;
            enemies_.push_back(e);
            spawnRing(p + Vec3{0, 0.03f, 0}, 1.2f, 0.9f, rgba(150, 90, 255));
        }
}

// --- Combate del protagonista -------------------------------------------------------------

// Enemigo preferido en la dirección `dir`: cerca y por delante.
int Game::pickTarget(Vec3 dir, float range) const {
    int best = -1;
    float bestScore = 1e9f;
    for (size_t i = 0; i < enemies_.size(); ++i) {
        const Enemy& e = enemies_[i];
        if (!e.targetable()) continue;
        Vec3 to = flat(e.body.pos - hero_.body.pos);
        float d = length(to);
        if (d > range || std::fabs(e.center().y - hero_.body.pos.y - 0.9f) > 3.0f) continue;
        float cosA = d > 1e-3f ? dot(to * (1.0f / d), dir) : 1.0f;
        if (cosA < -0.2f && d > 2.0f) continue;  // detrás y lejos: no
        float score = d * (1.6f - cosA);
        if (score < bestScore) {
            bestScore = score;
            best = int(i);
        }
    }
    return best;
}

// Fijar: el enemigo más centrado en la cámara; si ya hay uno, el siguiente.
void Game::cycleLock(Vec3 camForward) {
    Vec3 fwd = normalize(flat(camForward));
    std::vector<std::pair<float, int>> cands;
    for (size_t i = 0; i < enemies_.size(); ++i) {
        const Enemy& e = enemies_[i];
        if (!e.targetable()) continue;
        Vec3 to = flat(e.body.pos - hero_.body.pos);
        float d = length(to);
        if (d > 25.0f) continue;
        float cosA = d > 1e-3f ? dot(to * (1.0f / d), fwd) : 1.0f;
        cands.push_back({(1.0f - cosA) * 10.0f + d * 0.15f, int(i)});
    }
    if (cands.empty()) {
        lock_ = -1;
        return;
    }
    std::sort(cands.begin(), cands.end());
    if (lock_ < 0) {
        lock_ = cands[0].second;
        return;
    }
    for (size_t i = 0; i < cands.size(); ++i)
        if (cands[i].second == lock_) {
            lock_ = cands[(i + 1) % cands.size()].second;
            return;
        }
    lock_ = cands[0].second;
}

float Game::cameraClear(Vec3 from, Vec3 to) const {
    RayHit h;
    return segmentVsWorld(from, to, world_, h) ? h.t : 1.0f;
}

bool Game::lockTarget(Vec3& pos) const {
    if (lock_ < 0) return false;
    pos = enemies_[size_t(lock_)].center();
    return true;
}

// Ataque: hacia el objetivo fijado o, si no hay, hacia el enemigo más a mano en la
// dirección que se pulsa. Si está algo lejos, el golpe empieza con un acercamiento.
bool Game::heroAttack(Vec3 moveInput) {
    Vec3 dir = length(moveInput) > 0.1f ? normalize(flat(moveInput)) : hero_.forward();
    int t = lock_ >= 0 ? lock_ : pickTarget(dir, 6.0f);
    float yaw = std::atan2(dir.x, dir.z), dash = 0;
    if (t >= 0) {
        Vec3 to = flat(enemies_[size_t(t)].body.pos - hero_.body.pos);
        float d = length(to);
        if (d > 1e-3f) yaw = std::atan2(to.x, to.z);
        float reach = inv_.weaponStats().reach;
        if (d > reach * 0.8f && d < 7.0f) dash = std::min(d - reach * 0.7f, 5.0f);
    }
    // Con un orbe enemigo cerca, el golpe es para devolverlo: sin acercamiento (que
    // metería al personaje en el orbe) y hacia el orbe.
    for (const Projectile& p : projectiles_) {
        Vec3 to = flat(p.pos - hero_.body.pos);
        if (p.kind == ProjectileKind::EnemyOrb && !p.reflected && length(to) < 3.5f) {
            dash = 0;
            if (length(to) > 0.1f) yaw = std::atan2(to.x, to.z);
        }
    }
    return hero_.attack(yaw, dash);
}

void Game::runCommand(int index) {
    if (index < 3) {
        castSpell(Spell(index));
        return;
    }
    ItemId id = inv_.quick[index - 3];
    if (inv_.count(id) <= 0) {
        notify(std::string("No te queda: ") + itemName(id), rgba(180, 180, 190));
        return;
    }
    useItem(id);
}

bool Game::castSpell(Spell s) {
    const SpellDef& d = spellDef(s);
    if (mp_ < d.cost) {
        notify("PM insuficientes", rgba(150, 170, 255));
        return false;
    }
    Vec3 dir = hero_.forward();
    int t = lock_ >= 0 ? lock_ : pickTarget(dir, 18.0f);
    float yaw = hero_.yaw;
    if (t >= 0 && s != Spell::Cure) {
        Vec3 to = flat(enemies_[size_t(t)].body.pos - hero_.body.pos);
        if (length(to) > 1e-3f) yaw = std::atan2(to.x, to.z);
    }
    if (!hero_.cast(yaw)) return false;
    mp_ -= d.cost;
    pendingSpell_ = s;
    return true;
}

Vec3 Game::bladeTip() const {
    return transformPoint(hero_.joints[WristR], kBladeBase + Vec3{0, 0, inv_.weaponStats().length});
}

void Game::castImpact(Spell s) {
    const WeaponStats& w = inv_.weaponStats();
    Vec3 fwd = hero_.forward();
    Vec3 origin = bladeTip();
    int t = lock_ >= 0 ? lock_ : pickTarget(fwd, 18.0f);
    Vec3 aim = fwd;
    if (t >= 0) aim = normalize(enemies_[size_t(t)].center() - origin);
    switch (s) {
        case Spell::Fire: {
            Projectile p{ProjectileKind::Fire, origin, aim * 14.0f, 1.6f, t};
            p.seed = random01() * 100.0f;
            projectiles_.push_back(p);
            spawnSparks(origin, fwd, 10, rgba(255, 170, 60), 3.0f);
            break;
        }
        case Spell::Ice:
            for (float a : {-0.22f, 0.0f, 0.22f}) {
                Vec3 d = normalize(Vec3{aim.x * std::cos(a) + aim.z * std::sin(a), aim.y,
                                        -aim.x * std::sin(a) + aim.z * std::cos(a)});
                Projectile p{ProjectileKind::Ice, origin, d * 18.0f, 0.7f, a == 0.0f ? t : -1};
                p.seed = random01() * 100.0f;
                projectiles_.push_back(p);
            }
            spawnSparks(origin, fwd, 12, rgba(170, 235, 255), 3.0f);
            break;
        case Spell::Cure: {
            heal(35 + w.magic * 2.5f, 0);
            Vec3 base = hero_.body.pos;
            spawnRing(base + Vec3{0, 0.05f, 0}, 1.8f, 0.8f, rgba(120, 255, 150));
            for (int i = 0; i < 36 && particles_.size() < kMaxParticles; ++i) {
                float a = random01() * 2.0f * kPi, r = randRange(0.3f, 0.9f);
                Vec3 p = base + Vec3{std::cos(a) * r, randRange(0.0f, 1.6f), std::sin(a) * r};
                particles_.push_back({p, {0, randRange(0.8f, 2.0f), 0}, 1.0f, 1.0f, randRange(0.08f, 0.16f),
                                      i % 3 ? rgba(130, 255, 160) : rgba(255, 255, 200), 0, true});
            }
            break;
        }
        case Spell::Count: break;
    }
}

void Game::resolveImpact(Move m) {
    if (m == Move::Cast) {
        castImpact(pendingSpell_);
        return;
    }
    const WeaponStats& w = inv_.weaponStats();
    const HitParams& hp = kHits[int(m)];
    float range = w.reach + hp.extraRange;
    Vec3 origin = hero_.body.pos, fwd = hero_.forward();
    float chestY = origin.y + 1.0f;
    lastHitOrigin_ = origin;
    lastHitDir_ = fwd;
    lastHitRange_ = range;
    lastHitCos_ = hp.cosArc;
    lastHitAge_ = 0;

    auto inArc = [&](Vec3 to, float extra) {
        Vec3 d = flat(to);
        float dist = length(d);
        if (dist > range + extra) return false;
        return dist < extra + 0.3f || dot(d * (1.0f / dist), fwd) >= hp.cosArc;
    };
    int hits = 0;
    for (Enemy& e : enemies_) {
        if (!e.targetable()) continue;
        Vec3 to = e.center() - origin;
        if (std::fabs(e.center().y - chestY) > 1.5f || !inArc(to, e.body.radius)) continue;
        Vec3 dir = normalize(flat(to));
        if (length(dir) < 0.5f) dir = fwd;
        hitEnemy(e, dir, w.power * hp.damage, hp.knock, hp.lift, hp.heavy);
        ++hits;
    }
    for (Crate& c : crates_) {
        if (c.broken || std::fabs(c.box.center().y - chestY) > 1.5f || !inArc(c.box.center() - origin, 0.4f)) continue;
        c.hitFlash = 0.15f;
        spawnSparks(c.box.center(), fwd, 6, rgba(230, 190, 120), 3.0f, false);
        if ((c.hp -= 1) <= 0) breakCrate(c);
        ++hits;
    }
    // Los orbes de las polillas se pueden devolver de un golpe.
    for (Projectile& p : projectiles_) {
        if (p.kind != ProjectileKind::EnemyOrb || p.reflected || p.dead) continue;
        if (std::fabs(p.pos.y - chestY) > 1.5f || !inArc(p.pos - origin, 0.5f)) continue;
        p.reflected = true;
        p.life = 3.0f;
        int t = pickTarget(fwd, 25.0f);
        p.target = t;
        Vec3 aim = t >= 0 ? normalize(enemies_[size_t(t)].center() - p.pos) : fwd;
        p.vel = aim * 14.0f;
        spawnSparks(p.pos, aim, 14, rgba(255, 255, 200), 4.0f);
        ++hits;
    }
    if (hits > 0) {
        hitStop_ = isFinisher(m) ? 0.09f : 0.05f;
        shake_ = std::max(shake_, isFinisher(m) ? 0.35f : 0.12f);
    }
    if (m == Move::Finisher) {
        shake_ = std::max(shake_, 0.25f);
        spawnRing(origin + Vec3{0, 0.05f, 0}, range, 0.45f, w.trail);
        spawnSparks(origin + fwd * 1.2f, {0, 1, 0}, 16, w.trail, 5.0f);
    }
}

void Game::hitEnemy(Enemy& e, Vec3 dir, float damage, float knock, float lift, bool heavy) {
    e.health -= damage;
    e.hitFlash = 0.12f;
    Vec3 point = e.center() - dir * (e.body.radius * 0.6f);
    spawnSparks(point, dir, 8, rgba(255, 255, 230), 4.0f);
    spawnSparks(point, dir, 5, inv_.weaponStats().trail, 3.0f);
    const EnemyDef& d = e.def();
    if (e.health <= 0) {
        e.knock = dir * knock;
        killEnemy(e);
        return;
    }
    // Las setas, mientras saltan para aplastar, solo se detienen con un remate.
    bool armored = e.kind == EnemyKind::Shroom && e.state == AiState::Attack && !heavy;
    if (armored) return;
    e.state = AiState::Stagger;
    e.stateTime = 0;
    e.knock = dir * (knock * (d.flying ? 0.6f : 1.0f));
    if (!d.flying) e.body.vel.y = std::max(e.body.vel.y, lift * (e.kind == EnemyKind::Shroom ? 0.6f : 1.0f));
}

void Game::killEnemy(Enemy& e) {
    const EnemyDef& d = e.def();
    e.state = AiState::Dying;
    e.stateTime = 0;
    e.health = 0;
    ++kills_;
    for (size_t i = 0; i < enemies_.size(); ++i)
        if (&enemies_[i] == &e && lock_ == int(i)) lock_ = -1;
    Vec3 c = e.center();
    uint32_t col = enemyColor(e.kind);
    spawnSparks(c, {0, 1, 0}, 24, col, 5.0f);
    spawnSparks(c, {0, 1, 0}, 10, rgba(255, 255, 255), 3.0f);
    spawnRing(Vec3{c.x, groundHeight(c) + 0.05f, c.z}, 1.6f, 0.5f, col);
    // El "alma" del enemigo: un destello grande que sube despacio.
    particles_.push_back({c, {0, 1.6f, 0}, 1.3f, 1.3f, 0.5f, rgba(255, 150, 220), -0.2f, true});

    // Botín: dinero siempre, orbes casi siempre y, a veces, un objeto.
    int munny = d.munnyMin + int(random01() * float(d.munnyMax - d.munnyMin + 1));
    int hpOrbs = random01() < 0.7f ? 1 + int(random01() * 2.0f) : 0;
    int mpOrbs = random01() < 0.45f ? 1 + int(random01() * 2.0f) : 0;
    float r = random01();
    ItemId item = ItemId::Potion;
    bool withItem = true;
    switch (e.kind) {
        case EnemyKind::Imp:
            if (r < 0.30f) item = ItemId::ShinyShard;
            else if (r < 0.42f) item = ItemId::Potion;
            else withItem = false;
            break;
        case EnemyKind::Shroom:
            if (r < 0.40f) item = ItemId::GoldSpore;
            else if (r < 0.60f) item = ItemId::Potion;
            else if (r < 0.68f) item = ItemId::HiPotion;
            else withItem = false;
            break;
        default:
            if (r < 0.35f) item = ItemId::WingDust;
            else if (r < 0.50f) item = ItemId::Ether;
            else withItem = false;
            break;
    }
    dropLoot(c, munny, hpOrbs, mpOrbs, item, withItem);
}

void Game::hurtHero(Vec3 dir, float damage, float knock, Enemy* source) {
    if (hero_.downTimer >= 0 || hero_.rolling() || heroInvuln_ > 0) return;
    Vec3 chest = hero_.body.pos + Vec3{0, 1.0f, 0};
    // Guardia de frente: no hace daño. Si se acaba de levantar, rechaza al atacante.
    if (hero_.guarding() && dot(dir, hero_.forward()) < -0.2f) {
        perfectGuard_ = hero_.guardTime() < kPerfectGuard;
        guardFlash_ = 0.3f;
        spawnSparks(chest + hero_.forward() * 0.5f, -dir, perfectGuard_ ? 22 : 10,
                    perfectGuard_ ? rgba(160, 230, 255) : rgba(255, 245, 210), 4.0f);
        hero_.push(dir * (knock * 0.3f));
        if (source) {
            source->state = AiState::Stagger;
            source->stateTime = 0;
            source->knock = -dir * (perfectGuard_ ? 7.0f : 3.0f);
        }
        if (perfectGuard_) hitStop_ = 0.08f;
        return;
    }
    hp_ = std::max(0.0f, hp_ - damage);
    heroInvuln_ = kInvulnAfterHit;
    heroFlash_ = 0.25f;
    shake_ = std::max(shake_, 0.25f);
    hero_.flinch(0.35f);
    hero_.push(dir * knock + Vec3{0, 2.0f, 0});
    spawnSparks(chest, dir, 10, rgba(255, 90, 90), 3.0f);
    if (hp_ <= 0) defeatHero();
}

void Game::defeatHero() {
    hp_ = 0;
    hero_.downTimer = 0;
    lock_ = -1;
    trail_.clear();
}

void Game::respawnHero() {
    hero_.reset();
    applyWeapon();
    hero_.body.pos = heroStart_;
    hp_ = hpGhost_ = maxHp_;
    mp_ = maxMp_;
    heroInvuln_ = 2.0f;
    spawnRing(heroStart_ + Vec3{0, 0.05f, 0}, 1.5f, 0.8f, rgba(255, 255, 200));
}

// --- Enemigos ------------------------------------------------------------------------

void Game::updateEnemy(Enemy& e, float dt) {
    const EnemyDef& d = e.def();
    for (int i = 0; i < d.jointCount; ++i) e.prevJoints[i] = e.joints[i];
    e.prevPos = e.body.pos;
    float slow = e.freeze > 0 ? 0.45f : 1.0f;  // congelado: todo más lento
    float edt = dt * slow;
    e.stateTime += edt;
    e.hitFlash = std::max(0.0f, e.hitFlash - dt);
    e.cooldown = std::max(0.0f, e.cooldown - edt);
    e.freeze = std::max(0.0f, e.freeze - dt);
    if (e.burn > 0 && e.alive()) {
        e.burn -= dt;
        e.health -= 6.0f * dt;
        if (random01() < dt * 25.0f && particles_.size() < kMaxParticles)
            particles_.push_back({e.center() + Vec3{randRange(-0.3f, 0.3f), randRange(-0.3f, 0.3f), randRange(-0.3f, 0.3f)},
                                  {0, randRange(1.0f, 2.0f), 0}, 0.5f, 0.5f, 0.1f, rgba(255, 150, 50), -1.0f, false});
        if (e.health <= 0) killEnemy(e);
    }

    Vec3 toHero = flat(hero_.body.pos - e.body.pos);
    float dist = length(toHero);
    Vec3 dirHero = dist > 1e-4f ? toHero * (1.0f / dist) : e.forward();
    bool heroOk = hero_.downTimer < 0;
    Vec3 desired;  // velocidad horizontal deseada
    bool faceHero = false;

    switch (e.state) {
        case AiState::Spawning:
            if (e.stateTime > 0.9f) {
                e.state = AiState::Idle;
                e.stateTime = 0;
            }
            break;
        case AiState::Idle: {
            float a = time_ * 0.5f + e.seed;
            desired = Vec3{std::sin(a), 0, std::cos(a)} * (d.speed * 0.25f);
            if (heroOk && dist < d.aggroRange) {
                e.state = AiState::Chase;
                e.stateTime = 0;
            }
            break;
        }
        case AiState::Chase: {
            if (!heroOk) {
                e.state = AiState::Idle;
                break;
            }
            faceHero = true;
            if (d.flying) {
                // Vuela en círculos a media distancia.
                float radial = dist > 6.5f ? 1.0f : dist < 4.0f ? -1.0f : 0.0f;
                Vec3 tangent{-dirHero.z, 0, dirHero.x};
                if (std::fmod(e.seed, 2.0f) < 1.0f) tangent = -tangent;
                desired = (dirHero * radial + tangent * 0.7f) * d.speed;
            } else if (dist > d.radius + hero_.body.radius + 0.25f) {
                desired = dirHero * d.speed;
            }
            float dy = std::fabs(hero_.body.pos.y - e.body.pos.y);
            if (dist < d.attackRange && e.cooldown <= 0 && (d.flying || dy < 1.2f)) {
                e.state = AiState::Windup;
                e.stateTime = 0;
            }
            break;
        }
        case AiState::Windup:
            faceHero = true;
            if (e.stateTime > d.windup) enemyStartAttack(e);
            break;
        case AiState::Attack: {
            Vec3 heroChest = hero_.body.pos + Vec3{0, 0.9f, 0};
            if (e.kind == EnemyKind::Imp) {
                if (!e.attackHit && length(e.center() - heroChest) < 0.95f + e.body.radius) {
                    e.attackHit = true;
                    hurtHero(e.forward(), d.damage, d.knock, &e);
                }
                if (e.stateTime > d.attackTime) {
                    e.state = AiState::Recover;
                    e.stateTime = 0;
                }
            } else if (e.kind == EnemyKind::Shroom) {
                // Aterriza: onda alrededor. Saltar a tiempo la esquiva.
                if ((e.body.grounded && e.stateTime > 0.2f) || e.stateTime > 2.0f) {
                    const float radius = 2.6f;
                    spawnRing(e.body.pos + Vec3{0, 0.05f, 0}, radius, 0.45f, rgba(90, 240, 220));
                    spawnSparks(e.body.pos, {0, 1, 0}, 12, rgba(255, 230, 120), 4.0f, false);
                    shake_ = std::max(shake_, 0.2f);
                    bool heroLow = std::fabs(hero_.body.pos.y - e.body.pos.y) < 0.35f && hero_.body.grounded;
                    if (heroLow && dist < radius + hero_.body.radius) hurtHero(dirHero, d.damage, d.knock, &e);
                    e.state = AiState::Recover;
                    e.stateTime = 0;
                    e.knock = {};
                }
            } else if (e.stateTime > d.attackTime) {
                e.state = AiState::Recover;
                e.stateTime = 0;
            }
            break;
        }
        case AiState::Recover:
            if (e.stateTime > d.recover) {
                e.state = AiState::Chase;
                e.stateTime = 0;
                e.cooldown = d.cooldown * randRange(0.8f, 1.3f);
            }
            break;
        case AiState::Stagger:
            if (e.stateTime > 0.45f) {
                e.state = AiState::Chase;
                e.stateTime = 0;
                e.cooldown = std::max(e.cooldown, 0.3f);
            }
            break;
        case AiState::Dying: break;
    }

    // Orientación: hacia el jugador o hacia donde camina.
    if (faceHero) {
        float target = std::atan2(dirHero.x, dirHero.z);
        e.yaw = wrapAngle(e.yaw + clampf(wrapAngle(target - e.yaw), -8.0f * edt, 8.0f * edt));
    } else if (length(desired) > 0.1f && e.state == AiState::Idle) {
        float target = std::atan2(desired.x, desired.z);
        e.yaw = wrapAngle(e.yaw + clampf(wrapAngle(target - e.yaw), -4.0f * edt, 4.0f * edt));
    }
    e.phase += (d.flying ? 14.0f : 2.0f + length(desired) * 3.0f) * edt;

    if (e.state == AiState::Spawning) {
        animateEnemy(e, time_, dt);
        return;
    }
    e.knock = e.knock * std::exp(-(e.body.grounded || d.flying ? 6.0f : 0.5f) * dt);
    Vec3 v = desired * slow + e.knock;
    e.body.vel.x = v.x;
    e.body.vel.z = v.z;
    if (d.flying) {
        // Mantiene la altura sobre el suelo (moveBody añade la gravedad: se compensa).
        float targetY = groundHeight(e.body.pos + Vec3{0, 0.5f, 0}) + d.hoverHeight - d.height * 0.5f;
        if (e.state == AiState::Stagger) targetY -= 0.4f;
        if (e.state == AiState::Dying) targetY = e.body.pos.y;
        e.body.vel.y = (targetY - e.body.pos.y) * 3.0f + kGravity * dt;
    }
    moveBody(e.body, world_, dt, 0.3f);
    e.body.pos.x = clampf(e.body.pos.x, -kArena, kArena);
    e.body.pos.z = clampf(e.body.pos.z, -kArena, kArena);
    animateEnemy(e, time_, dt);
}

void Game::enemyStartAttack(Enemy& e) {
    e.state = AiState::Attack;
    e.stateTime = 0;
    e.attackHit = false;
    Vec3 to = flat(hero_.body.pos - e.body.pos);
    float dist = length(to);
    switch (e.kind) {
        case EnemyKind::Imp:  // salto con zarpazo
            e.knock = e.forward() * 9.5f;
            e.body.vel.y = 2.5f;
            break;
        case EnemyKind::Shroom:  // salta hacia el jugador para caerle encima
            e.body.vel.y = 7.5f;
            e.knock = (dist > 1e-3f ? to * (1.0f / dist) : e.forward()) * (std::min(dist, 3.5f) / 0.75f);
            break;
        case EnemyKind::Moth: {  // orbe de luz que persigue un poco al jugador
            Vec3 origin = e.center() + e.forward() * 0.35f;
            Vec3 aim = normalize(hero_.body.pos + Vec3{0, 1.0f, 0} - origin);
            Projectile p{ProjectileKind::EnemyOrb, origin, aim * 7.0f, 4.0f};
            p.seed = random01() * 100.0f;
            projectiles_.push_back(p);
            spawnSparks(origin, aim, 8, rgba(255, 180, 240), 2.0f);
            break;
        }
        case EnemyKind::Count: break;
    }
}

// --- Proyectiles (magias y orbes enemigos) -------------------------------------------------

void Game::updateProjectiles(float dt) {
    Vec3 heroChest = hero_.body.pos + Vec3{0, 1.0f, 0};
    for (Projectile& p : projectiles_) {
        if (p.dead) continue;
        if ((p.life -= dt) <= 0) {
            if (p.kind == ProjectileKind::Fire) explodeFire(p.pos);
            p.dead = true;
            continue;
        }
        bool hostile = p.kind == ProjectileKind::EnemyOrb && !p.reflected;
        // Persecución suave del objetivo.
        float speed = length(p.vel);
        if (p.target >= int(enemies_.size()) || (p.target >= 0 && !enemies_[size_t(p.target)].targetable())) p.target = -1;
        Vec3 goal;
        float steer = 0;
        if (hostile && hero_.downTimer < 0) {
            goal = heroChest;
            steer = 1.2f;
        } else if (p.target >= 0) {
            goal = enemies_[size_t(p.target)].center();
            steer = 5.0f;
        }
        if (steer > 0) p.vel = normalize(p.vel + (normalize(goal - p.pos) * speed - p.vel) * std::min(1.0f, steer * dt)) * speed;

        Vec3 next = p.pos + p.vel * dt;
        // Distancia del segmento recorrido a un punto (para chocar con cuerpos redondos).
        auto segDist = [&](Vec3 c) {
            Vec3 ab = next - p.pos;
            float t = clampf(dot(c - p.pos, ab) / std::max(dot(ab, ab), 1e-6f), 0, 1);
            return length(p.pos + ab * t - c);
        };
        if (hostile) {
            if (!hero_.rolling() && hero_.downTimer < 0 && segDist(heroChest) < 0.55f) {
                hurtHero(normalize(flat(p.vel)), enemyDef(EnemyKind::Moth).damage, enemyDef(EnemyKind::Moth).knock,
                         nullptr);
                spawnSparks(p.pos, -p.vel * 0.1f, 10, rgba(255, 180, 240), 3.0f);
                p.dead = true;
                continue;
            }
        } else {
            Enemy* hit = nullptr;
            for (Enemy& e : enemies_)
                if (e.targetable() && segDist(e.center()) < e.body.radius + 0.25f) {
                    hit = &e;
                    break;
                }
            if (hit) {
                Vec3 dir = normalize(flat(p.vel));
                float magic = inv_.weaponStats().magic;
                if (p.kind == ProjectileKind::Fire) {
                    explodeFire(p.pos);
                } else if (p.kind == ProjectileKind::Ice) {
                    hit->freeze = 3.0f;
                    hitEnemy(*hit, dir, 6 + magic, 2.0f, 0.5f, false);
                    spawnSparks(p.pos, dir, 14, rgba(180, 240, 255), 3.0f);
                } else {
                    hitEnemy(*hit, dir, 20, 4.0f, 1.0f, false);
                }
                p.dead = true;
                continue;
            }
        }
        RayHit h;
        if (segmentVsWorld(p.pos, next, world_, h)) {
            Vec3 point = p.pos + (next - p.pos) * h.t;
            if (p.kind == ProjectileKind::Fire)
                explodeFire(point + h.normal * 0.1f);
            else
                spawnSparks(point, h.normal, 8, p.kind == ProjectileKind::Ice ? rgba(180, 240, 255) : rgba(255, 180, 240), 2.5f);
            p.dead = true;
            continue;
        }
        p.pos = next;
    }
    projectiles_.erase(std::remove_if(projectiles_.begin(), projectiles_.end(), [](const Projectile& p) { return p.dead; }),
                       projectiles_.end());
}

void Game::explodeFire(Vec3 point) {
    const float radius = 2.2f;
    float magic = inv_.weaponStats().magic;
    blasts_.push_back({point, 0, 1.6f, random01() * 100.0f});
    spawnSparks(point, {0, 1, 0}, 18, rgba(255, 170, 60), 5.0f);
    spawnRing(Vec3{point.x, groundHeight(point) + 0.05f, point.z}, radius, 0.4f, rgba(255, 150, 60));
    shake_ = std::max(shake_, 0.15f);
    for (Enemy& e : enemies_) {
        if (!e.targetable()) continue;
        Vec3 to = e.center() - point;
        float d = length(to);
        if (d > radius + e.body.radius) continue;
        float falloff = 1.0f - 0.5f * clampf(d / radius, 0, 1);
        Vec3 dir = normalize(flat(to));
        if (length(dir) < 0.5f) dir = hero_.forward();
        e.burn = 2.5f;
        hitEnemy(e, dir, (8 + magic * 2.0f) * falloff, 5.0f, 2.5f, true);
    }
    for (Crate& c : crates_)
        if (!c.broken && length(c.box.center() - point) < radius) breakCrate(c);
}

// --- Botín ----------------------------------------------------------------------------

void Game::dropLoot(Vec3 pos, int munny, int hpOrbs, int mpOrbs, ItemId item, bool withItem) {
    auto pop = [&](PickupKind kind, ItemId id, int value) {
        float a = random01() * 2.0f * kPi, r = randRange(1.2f, 2.8f);
        Pickup p;
        p.kind = kind;
        p.item = id;
        p.value = value;
        p.pos = pos;
        p.vel = {std::cos(a) * r, randRange(4.5f, 6.5f), std::sin(a) * r};
        p.seed = random01() * 10.0f;
        pickups_.push_back(p);
    };
    while (munny > 0) {  // monedas de 10, 5 y 1
        int v = munny >= 10 ? 10 : munny >= 5 ? 5 : 1;
        pop(PickupKind::Munny, ItemId::Potion, v);
        munny -= v;
    }
    for (int i = 0; i < hpOrbs; ++i) pop(PickupKind::Hp, ItemId::Potion, 0);
    for (int i = 0; i < mpOrbs; ++i) pop(PickupKind::Mp, ItemId::Potion, 0);
    if (withItem) pop(PickupKind::Item, item, 1);
}

void Game::updatePickups(float dt) {
    Vec3 chest = hero_.body.pos + Vec3{0, 0.9f, 0};
    bool heroOk = hero_.downTimer < 0;
    for (Pickup& p : pickups_) {
        p.age += dt;
        if (p.age > (p.kind == PickupKind::Item ? 60.0f : 25.0f)) {
            p.dead = true;
            continue;
        }
        Vec3 to = chest - p.pos;
        float dist = length(to);
        float magnet = p.kind == PickupKind::Item ? 2.5f : 4.5f;
        if (heroOk && p.age > 0.4f && dist < magnet) {
            // Atraído por el jugador, cada vez más rápido.
            p.resting = false;
            float spd = 6.0f + (magnet - dist) * 5.0f;
            p.vel = p.vel + (to * (spd / std::max(dist, 1e-3f)) - p.vel) * std::min(1.0f, 10.0f * dt);
            p.pos += p.vel * dt;
            if (length(chest - p.pos) < 0.5f) collect(p);
            continue;
        }
        if (p.resting) continue;
        p.vel.y -= kGravity * dt;
        p.vel.x *= std::exp(-0.8f * dt);
        p.vel.z *= std::exp(-0.8f * dt);
        Vec3 next = p.pos + p.vel * dt;
        RayHit h;
        if (segmentVsWorld(p.pos, next, world_, h)) {
            Vec3 n = h.normal;
            p.pos = p.pos + (next - p.pos) * h.t + n * 0.01f;
            p.vel = (p.vel - n * (1.5f * dot(p.vel, n))) * 0.55f;
            if (n.y > 0.7f && length(p.vel) < 1.2f) {
                p.resting = true;
                p.vel = {};
            }
        } else {
            p.pos = next;
        }
    }
    pickups_.erase(std::remove_if(pickups_.begin(), pickups_.end(), [](const Pickup& p) { return p.dead; }),
                   pickups_.end());
}

void Game::collect(Pickup& p) {
    p.dead = true;
    uint32_t col = rgba(255, 255, 255);
    switch (p.kind) {
        case PickupKind::Hp: heal(kHpOrb, 0); col = rgba(120, 255, 140); break;
        case PickupKind::Mp: heal(0, kMpOrb); col = rgba(120, 170, 255); break;
        case PickupKind::Munny: inv_.munny += p.value; col = rgba(255, 220, 80); break;
        case PickupKind::Item:
            col = itemDef(p.item).color;
            if (inv_.add(p.item) > 0)
                notify(std::string("Obtenido: ") + itemName(p.item), col);
            else
                notify(std::string("No cabe más: ") + itemName(p.item), rgba(180, 180, 190));
            break;
    }
    spawnSparks(p.pos, {0, 1, 0}, 4, col, 1.5f);
}

void Game::heal(float hp, float mp) {
    hp_ = std::min(maxHp_, hp_ + hp);
    mp_ = std::min(maxMp_, mp_ + mp);
}

bool Game::useItem(ItemId id) {
    const ItemDef& d = itemDef(id);
    if (d.kind != ItemKind::Consumable || inv_.count(id) <= 0 || hero_.downTimer >= 0) return false;
    bool useful = (d.heal > 0 && hp_ < maxHp_) || (d.mana > 0 && mp_ < maxMp_);
    if (!useful) {
        notify("Ahora no hace falta", rgba(180, 180, 190));
        return false;
    }
    inv_.remove(id);
    heal(d.heal, d.mana);
    spawnRing(hero_.body.pos + Vec3{0, 0.05f, 0}, 1.3f, 0.6f, d.color);
    for (int i = 0; i < 16 && particles_.size() < kMaxParticles; ++i) {
        float a = random01() * 2.0f * kPi;
        Vec3 p = hero_.body.pos + Vec3{std::cos(a) * 0.5f, randRange(0.2f, 1.4f), std::sin(a) * 0.5f};
        particles_.push_back({p, {0, randRange(1.0f, 2.0f), 0}, 0.8f, 0.8f, 0.1f, d.color, 0, true});
    }
    return true;
}

bool Game::equip(ItemId id) {
    if (itemDef(id).kind != ItemKind::Weapon || inv_.count(id) <= 0) return false;
    inv_.weapon = id;
    applyWeapon();
    return true;
}

int Game::nearbyChest() const {
    for (size_t i = 0; i < chests_.size(); ++i) {
        const TreasureChest& c = chests_[i];
        Vec3 d = flat(c.pos - hero_.body.pos);
        if (!c.open && length(d) < 1.7f && std::fabs(c.pos.y - hero_.body.pos.y) < 0.6f) return int(i);
    }
    return -1;
}

void Game::tryOpenChest() {
    int i = nearbyChest();
    if (i < 0 || hero_.busy()) return;
    TreasureChest& c = chests_[size_t(i)];
    c.open = true;
    c.openTime = 0;
    for (const auto& [id, n] : c.contents) {
        inv_.add(id, n);
        const ItemDef& d = itemDef(id);
        std::string text = d.kind == ItemKind::Weapon ? std::string("Nuevo prisma: ") + d.name
                                                      : std::string("Obtenido: ") + d.name;
        if (n > 1) text += " ×" + std::to_string(n);
        notify(text, d.color);
    }
    Vec3 top = c.pos + Vec3{0, 0.8f, 0};
    dropLoot(top, c.munny, 0, 0, ItemId::Potion, false);
    spawnSparks(top, {0, 1, 0}, 30, rgba(255, 230, 120), 4.0f);
    spawnRing(c.pos + Vec3{0, 0.05f, 0}, 1.5f, 0.6f, rgba(255, 230, 120));
}

void Game::breakCrate(Crate& c) {
    c.broken = true;
    rebuildSolids();
    Vec3 p = c.box.center();
    for (int i = 0; i < 18 && particles_.size() < kMaxParticles; ++i) {
        Vec3 v{randRange(-3, 3), randRange(2, 5), randRange(-3, 3)};
        particles_.push_back({p, v, 0.8f, 0.8f, 0.07f, i % 2 ? rgba(200, 150, 90) : rgba(150, 100, 60), 12.0f, false});
    }
    dropLoot(p, 2 + int(random01() * 4.0f), random01() < 0.5f ? 1 : 0, random01() < 0.25f ? 1 : 0, ItemId::Potion,
             random01() < 0.1f);
}

// --- Efectos ----------------------------------------------------------------------------

void Game::notify(const std::string& text, uint32_t color) {
    notices_.push_back({text, color, 0});
    if (notices_.size() > 5) notices_.erase(notices_.begin());
}

void Game::spawnSparks(Vec3 pos, Vec3 dir, int count, uint32_t color, float speed, bool stars) {
    for (int i = 0; i < count && particles_.size() < kMaxParticles; ++i) {
        Vec3 rnd{random01() - 0.5f, random01() - 0.3f, random01() - 0.5f};
        float life = randRange(0.25f, 0.55f);
        particles_.push_back({pos, dir * (speed * 0.5f) + rnd * (speed * 1.3f), life, life,
                              stars ? randRange(0.06f, 0.14f) : randRange(0.04f, 0.08f), color, stars ? 3.0f : 12.0f,
                              stars});
    }
}

void Game::spawnRing(Vec3 pos, float radius, float life, uint32_t color) { rings_.push_back({pos, 0, life, radius, color}); }

float Game::groundHeight(Vec3 p) const {
    RayHit h;
    Vec3 from{p.x, p.y, p.z}, to{p.x, -1.0f, p.z};
    if (from.y <= 0) return 0;
    return segmentVsWorld(from, to, world_, h) ? from.y + (to.y - from.y) * h.t : 0.0f;
}

void Game::updateEffects(float dt) {
    for (Particle& p : particles_) {
        p.vel.y -= p.gravity * dt;
        p.vel = p.vel * std::exp(-1.5f * dt);
        p.pos += p.vel * dt;
        p.life -= dt;
    }
    particles_.erase(std::remove_if(particles_.begin(), particles_.end(), [](const Particle& p) { return p.life <= 0; }),
                     particles_.end());
    // Surtidor de la fuente.
    if (random01() < dt * 40.0f && particles_.size() < kMaxParticles) {
        float a = random01() * 2.0f * kPi, r = randRange(1.2f, 2.0f);
        particles_.push_back({{0, 2.3f, 0}, {std::cos(a) * r, randRange(2.5f, 3.5f), std::sin(a) * r}, 0.9f, 0.9f,
                              0.05f, rgba(170, 225, 255), 9.8f, false});
    }
    for (Ring& r : rings_) r.age += dt;
    rings_.erase(std::remove_if(rings_.begin(), rings_.end(), [](const Ring& r) { return r.age > r.life; }), rings_.end());
    for (Blast& b : blasts_) b.age += dt;
    blasts_.erase(std::remove_if(blasts_.begin(), blasts_.end(), [](const Blast& b) { return b.age > 0.6f; }),
                  blasts_.end());
    for (Notice& n : notices_) n.age += dt;
    notices_.erase(std::remove_if(notices_.begin(), notices_.end(), [](const Notice& n) { return n.age > 4.0f; }),
                   notices_.end());
}
