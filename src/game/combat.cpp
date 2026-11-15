// Combate del protagonista: apuntar y fijar objetivo, golpes y combos, magias, daño a
// enemigos y al protagonista, muerte y reaparición.

#include "game/game.h"

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
    {1.2f, 0.9f, -1.0f,  4.5f, 1.5f, false},  // Slash3: giro con el látigo, golpea alrededor
    {2.2f, 0.9f, -1.0f,  9.0f, 5.5f, true},   // Finisher: martillazo que lanza por los aires
    {1.0f, 0.1f,  0.0f,  1.5f, 2.5f, false},  // Air1: mantiene al enemigo en el aire
    {1.0f, 0.1f,  0.0f,  1.5f, 2.5f, false},  // Air2
    {2.0f, 0.9f, -1.0f,  8.0f, 3.0f, true},   // AirFinisher: remolino con el látigo
    {0,    0,     1,     0,    0,    false},  // Cast
    {1.4f, 0.8f,  0.5f,  7.0f, 1.0f, true},   // Rush: embestida
    {1.8f, 0.4f, -1.0f,  6.0f, 2.0f, true},   // Counter: contraataque en giro
    {2.6f, 1.0f,  0.3f,  9.0f, 3.0f, true},   // Charged: corte cargado
    {1.2f, 0.0f,  0.3f,  1.0f, 9.0f, false},  // Launcher: lanza al enemigo hacia arriba
    {1.8f, 0.8f, -1.0f,  6.0f, 3.0f, true},   // Plunge: picado contra el suelo
    {1.1f, 0.6f,  0.7f,  3.5f, 0.6f, false},  // Thrust: estocada de lanza
    {0.6f,-0.4f,  0.3f,  1.5f, 0.3f, false},  // Punch
    {0.6f,-0.4f,  0.3f,  1.5f, 0.3f, false},  // PunchL
    {2.0f, 0.0f,  0.2f,  4.0f, 7.0f, true},   // Uppercut: gancho final
    {1.4f, 0.2f,  0.3f,  5.0f, 1.0f, true},   // Smash: mazazo de arriba abajo
    {1.1f, 0.4f, -0.3f,  7.0f, 1.5f, false},  // Sweep: barrido en media vuelta
    {2.6f, 1.6f, -1.0f,  9.0f, 6.0f, true},   // Quake: salto y martillazo que agrieta el suelo
};
const SpellDef kSpells[int(Spell::Count)] = {
    {"Lanza de luz", 20, rgba(255, 230, 150)},
    {"Refracción", 15, rgba(120, 220, 255)},
    {"Calidez", 35, rgba(255, 160, 200)},
};
// clang-format on

constexpr float kInvulnAfterHit = 0.8f;
constexpr float kPerfectGuard = 0.35f;  // bloquear al poco de levantar la guardia rechaza al atacante
constexpr float kCromaPerKill = 0.015f;

// Lo que suelta cada familia al liberarse: luz cálida o sombra violeta.
uint32_t enemyColor(EnemyKind k) { return enemyDef(k).shadow ? rgba(150, 80, 255) : rgba(255, 226, 160); }

}  // namespace

const SpellDef& spellDef(Spell s) { return kSpells[int(s)]; }

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
    return segmentVsWorld(from, to, cameraWorld_, h) ? h.t : 1.0f;
}

bool Game::lockTarget(Vec3& pos) const {
    if (inLight_ && lightLock_) {  // dentro de una fuente, la cámara mira a la elegida
        pos = lightLockPos_;
        return true;
    }
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
    pendingNova_ = false;
    return true;
}

// Punta del arma: el látigo de los giros es más largo que la hoja.
Vec3 Game::bladeTip() const {
    Move m = hero_.move();
    float len = inv_.weaponStats().length * (m == Move::Slash3 || m == Move::AirFinisher ? 2.0f : 1.0f);
    return transformPoint(hero_.joints[WristR], kBladeBase + Vec3{0, 0, len});
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
            sound(Sfx::Fire, origin);
            Projectile p{ProjectileKind::Fire, origin, aim * 14.0f, 1.6f, t};
            p.seed = random01() * 100.0f;
            projectiles_.push_back(p);
            spawnSparks(origin, fwd, 10, rgba(255, 170, 60), 3.0f);
            break;
        }
        case Spell::Ice:
            sound(Sfx::Ice, origin);
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
            uiSound(Sfx::Cure);
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
        if (pendingNova_) {
            pendingNova_ = false;
            supernova();
        } else {
            castImpact(pendingSpell_);
        }
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
        hitEnemy(e, dir, (w.power + inv_.powerBonus + levelPower()) * hp.damage * formDamage(), hp.knock * (form_ == Form::Colossus ? 1.4f : 1.0f),
                 hp.lift, hp.heavy || form_ == Form::Colossus);
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
    // Silbido del arma (según su forma) y golpe si ha alcanzado algo.
    bool heavyHead = m == Move::Finisher || m == Move::Smash || m == Move::Quake;
    Sfx swing = heavyHead ? Sfx::Hammer : (m == Move::Slash3 || m == Move::AirFinisher) ? Sfx::Whip : Sfx::Slash;
    sound(swing, origin, 0.8f, 0.95f + 0.1f * random01());
    if (hits > 0) sound(isFinisher(m) ? Sfx::HitHeavy : Sfx::Hit, origin + fwd, 1.0f, 0.9f + 0.2f * random01());
    if (hits > 0) {
        hitStop_ = isFinisher(m) ? 0.09f : 0.05f;
        shake_ = std::max(shake_, isFinisher(m) ? 0.35f : 0.12f);
    }
    // Formas: el Coloso suelta una onda de choque con cada golpe y el Eco un orbe de luz.
    if (form_ == Form::Colossus) {
        Vec3 at = origin + fwd * 1.2f;
        spawnRing(Vec3{at.x, groundHeight(at + Vec3{0, 1, 0}) + 0.05f, at.z}, 2.6f, 0.35f, formColor(form_));
        for (Enemy& e : enemies_)
            if (e.targetable() && length(flat(e.body.pos - at)) < 2.6f + e.body.radius)
                hitEnemy(e, normalize(flat(e.body.pos - at)), (w.power + inv_.powerBonus + levelPower()) * 0.5f, 4.0f, 2.0f, true);
        shake_ = std::max(shake_, 0.15f);
    } else if (form_ == Form::Echo) {
        int t = lock_ >= 0 ? lock_ : pickTarget(fwd, 15.0f);
        Vec3 from = bladeTip();
        Vec3 aim = t >= 0 ? normalize(enemies_[size_t(t)].center() - from) : fwd;
        Projectile p{ProjectileKind::EnemyOrb, from, aim * 15.0f, 2.0f, t};
        p.reflected = true;
        projectiles_.push_back(p);
    }
    if (m == Move::Charged) {  // corte cargado: lanza una onda de luz que lo atraviesa todo
        Projectile p{ProjectileKind::Wave, origin + Vec3{0, 1.0f, 0} + fwd * 0.8f, fwd * 16.0f, 0.7f};
        projectiles_.push_back(p);
        shake_ = std::max(shake_, 0.3f);
        sound(Sfx::Whip, origin, 1.0f, 0.7f);
    }
    if (m == Move::Finisher) {
        shake_ = std::max(shake_, 0.25f);
        spawnRing(origin + Vec3{0, 0.05f, 0}, range, 0.45f, w.trail);
        spawnSparks(origin + fwd * 1.2f, {0, 1, 0}, 16, w.trail, 5.0f);
    }
    if (m == Move::Smash) {  // el mazo toca el suelo: polvo oscuro y un anillo pequeño delante
        Vec3 at = origin + fwd * 1.3f;
        at.y = groundHeight(at + Vec3{0, 1, 0}) + 0.05f;
        spawnRing(at, 1.4f, 0.3f, w.trail);
        spawnSparks(at, {0, 1, 0}, 10, rgba(40, 24, 60), 3.0f, false);
        shake_ = std::max(shake_, 0.18f);
    }
    if (m == Move::Quake) {  // grietas de sombra alrededor y todos por los aires
        shake_ = std::max(shake_, 0.55f);
        hitStop_ = std::max(hitStop_, 0.08f);
        Vec3 at{origin.x, groundHeight(origin + Vec3{0, 1, 0}) + 0.05f, origin.z};
        spawnRing(at, range, 0.55f, w.trail);
        spawnRing(at, range * 0.6f, 0.4f, rgba(30, 16, 48));
        for (int i = 0; i < 8; ++i) {
            float a = float(i) * kPi * 0.25f + random01() * 0.3f;
            Vec3 d{std::cos(a), 0, std::sin(a)};
            spawnSparks(at + d * randRange(0.8f, range), {0, 1, 0}, 4, i % 2 ? w.trail : rgba(30, 16, 48), 5.0f, false);
        }
        sound(Sfx::FireBoom, at, 0.8f, 0.6f);
    }
}

void Game::hitEnemy(Enemy& e, Vec3 dir, float damage, float knock, float lift, bool heavy) {
    // La luz debilita a los de sombra y endurece a los de luz. Y la mitad que usa el
    // protagonista hiere más a la familia contraria.
    if (lightAt(e.body.pos) > 0.3f) damage *= e.def().shadow ? 1.5f : 0.8f;
    damage *= familyDamage(e);
    if (shadowMode_ && !e.def().shadow) shadow_ = std::min(maxShadow_, shadow_ + 1.5f);  // le arranca sombra
    e.health -= damage;
    e.rlReward -= damage / 15.0f;
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
    bool armored = (e.kind == EnemyKind::Brute && e.state == AiState::Attack && !heavy) || (e.boss && !heavy);
    if (armored) return;
    e.state = AiState::Stagger;
    e.stateTime = 0;
    e.knock = dir * (knock * (d.flying ? 0.6f : 1.0f));
    if (!d.flying) e.body.vel.y = std::max(e.body.vel.y, lift * (e.kind == EnemyKind::Brute ? 0.6f : 1.0f));
}

void Game::killEnemy(Enemy& e) {
    const EnemyDef& d = e.def();
    e.state = AiState::Dying;
    e.stateTime = 0;
    e.health = 0;
    ++kills_;
    croma_ = std::min(1.0f, croma_ + kCromaPerKill);
    if (e.rlState >= 0) brainFor(e).learn(e.rlState, e.intent, e.rlReward - 2.0f, 0, true);  // disipado
    e.rlState = -1;
    sound(Sfx::EnemyDie, e.center(), 1.0f, 0.9f + 0.25f * random01());
    playScene("primer_disipado");
    for (size_t i = 0; i < enemies_.size(); ++i)
        if (&enemies_[i] == &e && lock_ == int(i)) lock_ = -1;
    Vec3 c = e.center();
    uint32_t col = enemyColor(e.kind);
    spawnSparks(c, {0, 1, 0}, 24, col, 5.0f);
    spawnSparks(c, {0, 1, 0}, 10, rgba(255, 255, 255), 3.0f);
    Vec3 ground{c.x, groundHeight(c), c.z};
    spawnRing(ground + Vec3{0, 0.05f, 0}, 1.6f, 0.5f, col);
    // El color robado vuelve al mundo: salpica el suelo y se queda.
    // Lo que suelta se queda en el suelo: un poso cálido (luz) o un charco violáceo (sombra).
    splashes_.push_back({ground, randRange(1.1f, 1.6f), d.shadow ? rgba(46, 30, 74) : rgba(196, 176, 136), 0});
    if (splashes_.size() > size_t(Renderer::kMaxPaints)) splashes_.erase(splashes_.begin());
    for (int i = 0; i < 14 && particles_.size() < kMaxParticles; ++i) {
        float a = random01() * 2.0f * kPi;
        particles_.push_back({c, {std::cos(a) * randRange(2, 5), randRange(2, 5), std::sin(a) * randRange(2, 5)}, 0.7f, 0.7f,
                              0.09f, col, 9.8f, false});
    }
    // El "alma" del enemigo: un destello grande que sube despacio.
    particles_.push_back({c, {0, 1.6f, 0}, 1.3f, 1.3f, 0.5f, col, -0.2f, true});

    // Botín: dinero, experiencia y vida siempre, orbes de su familia casi siempre y, a
    // veces, un objeto. La experiencia crece con lo duro que es (y el jefe vale por siete).
    int munny = d.munnyMin + int(random01() * float(d.munnyMax - d.munnyMin + 1));
    int xp = int(d.maxHealth * 0.4f * (e.boss ? 7.0f : 1.0f));
    for (int left = xp; left > 0;) {  // en orbes de 10 como mucho
        Pickup p;
        p.kind = PickupKind::Xp;
        p.value = std::min(left, 10);
        left -= p.value;
        float a = random01() * 2.0f * kPi, r = randRange(0.8f, 2.0f);
        p.pos = c;
        p.vel = {std::cos(a) * r, randRange(5.0f, 7.0f), std::sin(a) * r};
        p.seed = random01() * 10.0f;
        pickups_.push_back(p);
    }
    int hpOrbs = 1 + (random01() < 0.5f ? 1 : 0) + (e.boss ? 4 : 0);
    int mpOrbs = random01() < 0.75f ? 1 + int(random01() * 2.0f) : 0;  // Luz o Sombra según la familia
    float r = random01();
    ItemId item = ItemId::Potion;
    bool withItem = true;
    switch (e.kind) {
        case EnemyKind::Dazzled:
            if (r < 0.30f) item = ItemId::ShinyShard;
            else if (r < 0.42f) item = ItemId::Potion;
            else withItem = false;
            break;
        case EnemyKind::Brute:
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
    if (e.boss) item = ItemId::BlazeGem, withItem = true;  // el jefe deja el prisma para la mejor espada
    dropLoot(c, munny, hpOrbs, mpOrbs, item, withItem, d.shadow);
}

void Game::hurtHero(Vec3 dir, float damage, float knock, Enemy* source) {
    if (hero_.downTimer >= 0 || hero_.rolling() || inLight_ || travel_ || diving_ || heroInvuln_ > 0) return;
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
        sound(perfectGuard_ ? Sfx::PerfectGuard : Sfx::Guard, chest);
        if (source) source->rlReward -= perfectGuard_ ? 1.0f : 0.5f;  // bloqueado: mala táctica
        return;
    }
    if (form_ == Form::Colossus) {  // el Coloso no se inmuta: menos daño, sin tambaleo
        damage *= 0.6f;
        knock *= 0.2f;
    }
    hp_ = std::max(0.0f, hp_ - damage);
    if (sandbox() && infHp_) hp_ = maxHp_;  // modo prueba: vida infinita
    if (source) source->rlReward += damage / 10.0f;  // le ha hecho daño: buena táctica
    sound(Sfx::HeroHurt, chest);
    heroInvuln_ = kInvulnAfterHit;
    heroFlash_ = 0.25f;
    shake_ = std::max(shake_, 0.25f);
    if (form_ != Form::Colossus) hero_.flinch(0.35f);
    hero_.push(dir * knock + Vec3{0, form_ == Form::Colossus ? 0.0f : 2.0f, 0});
    spawnSparks(chest, dir, 10, rgba(255, 90, 90), 3.0f);
    if (hp_ <= 0) defeatHero();
}

void Game::defeatHero() {
    hp_ = 0;
    inLight_ = travel_ = lightLock_ = diving_ = false;
    hero_.downTimer = 0;
    lock_ = -1;
    trail_.clear();
}

void Game::respawnHero() {
    resetEncounter();
    hero_.reset();
    applyWeapon();
    hero_.body.pos = checkpoint_;
    hero_.yaw = checkpointYaw_;
    hp_ = hpGhost_ = maxHp_;
    mp_ = maxMp_;
    heroInvuln_ = 2.0f;
    playScene("caida");
    spawnRing(checkpoint_ + Vec3{0, 0.05f, 0}, 1.5f, 0.8f, rgba(255, 255, 200));
}
