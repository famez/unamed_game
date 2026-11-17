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
    {"Lanza de luz",       20, rgba(255, 230, 150)},
    {"Refracción",         15, rgba(120, 220, 255)},
    {"Calidez",            35, rgba(255, 160, 200)},
    {"Estallido solar",    30, rgba(255, 190, 80)},
    {"Supernova",         100, rgba(255, 250, 220)},
    {"Púas de sombra",     20, rgba(170, 100, 255)},
    {"Cadenas del vacío",  25, rgba(120, 90, 200)},
    {"Drenar",             20, rgba(220, 70, 140)},
    {"Mina oscura",        30, rgba(140, 60, 255)},
    {"Agujero negro",      60, rgba(90, 40, 200)},
};
const Spell kLightSpells[kSpellSlots] = {Spell::Fire, Spell::Ice, Spell::Cure, Spell::Burst, Spell::Nova};
const Spell kShadowSpells[kSpellSlots] = {Spell::Spikes, Spell::Chains, Spell::Drain, Spell::DarkBomb, Spell::Hole};
// clang-format on

constexpr float kInvulnAfterHit = 0.8f;
constexpr float kPerfectGuard = 0.35f;  // bloquear al poco de levantar la guardia rechaza al atacante
constexpr float kParryStun = 2.2f;      // aturdido tras un bloqueo perfecto
constexpr float kRiposteWindow = 1.4f;  // para empezar el súper contraataque
constexpr float kRiposteDamage = 2.0f;
const uint32_t kRiposteShadow = rgba(150, 80, 255);
constexpr float kCromaPerKill = 0.015f;

// Lo que suelta cada familia al liberarse: luz cálida o sombra violeta.
uint32_t enemyColor(EnemyKind k) { return enemyDef(k).shadow ? rgba(150, 80, 255) : rgba(255, 226, 160); }

}  // namespace

const SpellDef& spellDef(Spell s) { return kSpells[int(s)]; }
Spell modeSpell(bool shadow, int slot) { return (shadow ? kShadowSpells : kLightSpells)[slot]; }

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
    if (index < kSpellSlots) {
        castSpell(modeSpell(shadowMode_, index));
        return;
    }
    ItemId id = inv_.quick[index - kSpellSlots];
    if (inv_.count(id) <= 0) {
        notify(std::string("No te queda: ") + itemName(id), rgba(180, 180, 190));
        return;
    }
    useItem(id);
}

bool Game::castSpell(Spell s) {
    const SpellDef& d = spellDef(s);
    if (s == Spell::Nova && !powers_.nova) return false;
    float cost = s == Spell::Nova ? maxMp_ : d.cost;  // la Supernova pide la barra llena
    if (mp_ < cost) {
        notify(s == Spell::Nova ? "La Supernova necesita el Poder lleno" : "Falta Poder", rgba(190, 160, 255));
        return false;
    }
    Vec3 dir = hero_.forward();
    int t = lock_ >= 0 ? lock_ : pickTarget(dir, 18.0f);
    float yaw = hero_.yaw;
    if (t >= 0 && s != Spell::Cure && s != Spell::Nova && s != Spell::Chains) {
        Vec3 to = flat(enemies_[size_t(t)].body.pos - hero_.body.pos);
        if (length(to) > 1e-3f) yaw = std::atan2(to.x, to.z);
    }
    if (!hero_.cast(yaw)) return false;
    mp_ -= cost;
    pendingSpell_ = s;
    if (s == Spell::Nova || s == Spell::Hole) uiSound(Sfx::Echo, 1.0f, s == Spell::Nova ? 0.8f : 0.45f);
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
        case Spell::Burst: {  // bola que vuela en arco y estalla al tocar algo
            sound(Sfx::Fire, origin, 1.0f, 0.7f);
            Vec3 to = spellSpot(20.0f) - origin;
            float d = length(flat(to));
            Vec3 v = flat(to) * (1.0f / std::max(d, 0.1f)) * 13.0f;
            v.y = clampf(0.5f * kGravity * (d / 13.0f) + to.y / std::max(d / 13.0f, 0.2f), 2.0f, 9.0f);
            Projectile p{ProjectileKind::Burst, origin, v, 2.5f, -1};
            p.seed = random01() * 100.0f;
            projectiles_.push_back(p);
            spawnSparks(origin, fwd, 12, rgba(255, 200, 100), 3.0f);
            break;
        }
        case Spell::Nova: supernova(); break;
        case Spell::Spikes: {  // púas que salen del suelo en línea y lanzan por los aires
            Vec3 start = hero_.body.pos + fwd * 0.8f;
            start.y = groundHeight(start + Vec3{0, 1, 0});
            Vec3 dir = normalize(flat(aim));
            if (length(dir) < 0.5f) dir = fwd;
            Projectile p{ProjectileKind::Spikes, start + Vec3{0, 0.9f, 0}, dir * 15.0f, 0.9f, -1};
            projectiles_.push_back(p);
            sound(Sfx::Hammer, start, 0.8f, 0.6f);
            shake_ = std::max(shake_, 0.15f);
            break;
        }
        case Spell::Chains: {  // cadenas desde el suelo: todos los de alrededor, quietos un rato
            Vec3 base = hero_.body.pos;
            int caught = 0;
            for (Enemy& e : enemies_) {
                if (!e.targetable() || length(flat(e.body.pos - base)) > 7.0f) continue;
                hitEnemy(e, normalize(flat(e.body.pos - base)), 4 + w.magic * 0.5f, 0, 0, false);
                if (!e.boss && e.alive()) {
                    e.state = AiState::Stagger;
                    e.stateTime = -2.2f;  // el aturdimiento dura ~2,6 s
                }
                Vec3 c = e.center();
                for (int k = 0; k < 10 && particles_.size() < kMaxParticles; ++k) {  // eslabones del suelo al Gris
                    float u = float(k) / 10.0f;
                    Vec3 floor{c.x + std::cos(float(k)) * 0.8f, groundHeight(c), c.z + std::sin(float(k)) * 0.8f};
                    particles_.push_back({floor + (c - floor) * u, {0, 0, 0}, 1.2f, 1.2f, 0.07f, k % 2 ? spellDef(s).color : rgba(30, 16, 48),
                                          0, true});
                }
                ++caught;
            }
            spawnRing(base + Vec3{0, 0.05f, 0}, 7.0f, 0.6f, spellDef(s).color);
            spawnRing(base + Vec3{0, 0.06f, 0}, 4.0f, 0.5f, rgba(30, 16, 48));
            sound(Sfx::WaveStart, base, 0.8f, 0.6f);
            if (caught == 0) notify("No hay nadie a quien encadenar", rgba(180, 160, 210));
            break;
        }
        case Spell::Drain: {  // le roba vida al Gris señalado y se la queda
            int target = t >= 0 ? t : pickTarget(fwd, 10.0f);
            if (target < 0) {
                notify("No hay nadie cerca a quien drenar", rgba(180, 160, 210));
                break;
            }
            Enemy& e = enemies_[size_t(target)];
            float dmg = 10 + w.magic * 2.0f + (w.power + levelPower()) * 0.8f;
            Vec3 c = e.center(), chest = hero_.body.pos + Vec3{0, 1.0f, 0};
            hitEnemy(e, normalize(flat(c - chest)), dmg, 1.0f, 0.5f, false);
            heal(dmg * 0.6f, 0);
            for (int k = 0; k < 30 && particles_.size() < kMaxParticles; ++k) {
                Vec3 from = c + Vec3{randRange(-0.3f, 0.3f), randRange(-0.3f, 0.3f), randRange(-0.3f, 0.3f)};
                particles_.push_back({from, (chest - from) * randRange(1.5f, 2.6f), 0.5f, 0.5f, 0.07f,
                                      k % 3 ? spellDef(s).color : rgba(255, 200, 220), 0, true});
            }
            sound(Sfx::Cure, chest, 0.7f, 0.6f);
            break;
        }
        case Spell::DarkBomb: {
            Vec3 at = spellSpot(14.0f) + Vec3{0, 0.6f, 0};
            holes_.push_back({at, 1.0f, 0, true});
            sound(Sfx::WaveStart, at, 0.6f, 1.6f);
            break;
        }
        case Spell::Hole: {
            Vec3 at = spellSpot(12.0f) + Vec3{0, 1.2f, 0};
            holes_.push_back({at, 2.6f, 0, false});
            shake_ = std::max(shake_, 0.3f);
            sound(Sfx::WaveStart, at, 0.7f, 1.4f);
            break;
        }
        case Spell::Count: break;
    }
}

// Dónde cae una técnica de área: en el objetivo (fijado o el que hay delante) o, si no hay
// ninguno, unos metros delante, en el suelo.
Vec3 Game::spellSpot(float range) const {
    int t = lock_ >= 0 ? lock_ : pickTarget(hero_.forward(), range);
    if (t >= 0) {
        Vec3 c = enemies_[size_t(t)].body.pos;
        return {c.x, groundHeight(c + Vec3{0, 1, 0}), c.z};
    }
    Vec3 p = hero_.body.pos + hero_.forward() * 4.0f;
    return {p.x, groundHeight(p + Vec3{0, 1, 0}), p.z};
}

void Game::resolveImpact(Move m) {
    if (m == Move::Cast) {
        castImpact(pendingSpell_);
        return;
    }
    const WeaponStats& w = inv_.weaponStats();
    const HitParams& hp = kHits[int(m)];
    float range = (form_ == Form::Wolf ? 2.3f : w.reach) + hp.extraRange;
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
        float riposte = riposteStep_ >= 0 ? kRiposteDamage : 1.0f;
        hitEnemy(e, dir, (w.power + inv_.powerBonus + levelPower()) * hp.damage * formDamage() * riposte,
                 hp.knock * (form_ != Form::None ? 1.3f : 1.0f) * (riposteStep_ >= 0 && riposteStep_ < riposteLength() ? 0.3f : 1.0f),
                 hp.lift, hp.heavy || form_ == Form::Wolf);
        ++hits;
    }
    for (Crate& c : crates_) {
        if (c.broken || std::fabs(c.box.center().y - chestY) > 1.5f || !inArc(c.box.center() - origin, 0.4f)) continue;
        c.hitFlash = 0.15f;
        spawnSparks(c.box.center(), fwd, 6, rgba(230, 190, 120), 3.0f, false);
        if ((c.hp -= 1) <= 0) breakCrate(c);
        ++hits;
    }
    // Las farolas se rompen a golpes (por el poste, a la altura del golpe).
    for (size_t i = 0; i < lamps_.size(); ++i) {
        const Lamp& l = lamps_[i];
        if (!l.bulb || l.broken || l.pos.y < chestY - 1.0f || l.pos.y > chestY + 5.0f) continue;
        Vec3 post{l.pos.x, chestY, l.pos.z};
        if (!inArc(post - origin, 0.4f)) continue;
        hitLamp(int(i), hp.heavy ? 2.0f : 1.0f);
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
    // Último golpe del súper contraataque: estallido de luz o corona de púas de sombra.
    if (riposteStep_ >= riposteLength() && isFinisher(m)) {
        hitStop_ = 0.22f;
        shake_ = std::max(shake_, 0.6f);
        if (!shadowMode_) {
            for (int k = -1; k <= 1; ++k) {
                Vec3 d = normalize(fwd + Vec3{-fwd.z, 0, fwd.x} * (0.35f * float(k)));
                projectiles_.push_back({ProjectileKind::Wave, origin + Vec3{0, 1.0f, 0} + d * 0.8f, d * 18.0f, 0.8f});
            }
            spawnRing(origin + Vec3{0, 0.05f, 0}, 5.0f, 0.5f, rgba(255, 220, 120));
            spawnSparks(origin + fwd + Vec3{0, 1, 0}, {0, 1, 0}, 40, rgba(255, 240, 190), 8.0f);
            novaFlash_ = std::max(novaFlash_, 0.25f);
        } else {
            for (int k = 0; k < 10; ++k) {
                float a = float(k) / 10.0f * 2.0f * kPi;
                Vec3 d{std::cos(a), 0, std::sin(a)};
                projectiles_.push_back({ProjectileKind::Spikes, origin + d * 0.6f + Vec3{0, 0.9f, 0}, d * 13.0f, 0.7f, -1});
            }
            spawnRing(origin + Vec3{0, 0.05f, 0}, 5.0f, 0.5f, kRiposteShadow);
            spawnSparks(origin + Vec3{0, 0.5f, 0}, {0, 1, 0}, 40, rgba(30, 15, 45), 7.0f, false);
        }
        notify("¡SÚPER CONTRAATAQUE!", shadowMode_ ? rgba(190, 130, 255) : rgba(255, 220, 120));
        voiceCooldown_ = 0;
        voice(VoiceLine::Laugh);
    }
    // Formas: la Solar remata con ondas de luz; el lobo recupera vida con cada mordisco.
    if (form_ == Form::Solar && isFinisher(m)) {
        Projectile p{ProjectileKind::Wave, origin + Vec3{0, 1.0f, 0} + fwd * 0.8f, fwd * 18.0f, 0.8f};
        projectiles_.push_back(p);
        spawnRing(origin + Vec3{0, 0.05f, 0}, 3.0f, 0.4f, formColor(form_));
    } else if (form_ == Form::Wolf && hits > 0) {
        heal(2.0f * float(hits), 0);
        spawnSparks(origin + fwd * 1.0f + Vec3{0, 0.8f, 0}, fwd, 6, rgba(40, 20, 60), 3.0f, false);
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
    if (!isSwarm(e.kind)) croma_ = std::min(1.0f, croma_ + kCromaPerKill);
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
    if (!isSwarm(e.kind)) {  // los bichos de un enjambre no dejan charco (serían decenas)
        splashes_.push_back({ground, randRange(1.1f, 1.6f), d.shadow ? rgba(46, 30, 74) : rgba(196, 176, 136), 0});
        if (splashes_.size() > size_t(Renderer::kMaxPaints)) splashes_.erase(splashes_.begin());
    }
    for (int i = 0; i < 14 && particles_.size() < kMaxParticles; ++i) {
        float a = random01() * 2.0f * kPi;
        particles_.push_back({c, {std::cos(a) * randRange(2, 5), randRange(2, 5), std::sin(a) * randRange(2, 5)}, 0.7f, 0.7f,
                              0.09f, col, 9.8f, false});
    }
    // El "alma" del enemigo: un destello grande que sube despacio.
    particles_.push_back({c, {0, 1.6f, 0}, 1.3f, 1.3f, 0.5f, col, -0.2f, true});
    if (e.kind == EnemyKind::Firefly) fireflyBurst(c);  // estalla y enciende lo que toca
    if (isSwarm(e.kind)) {  // un bicho de un golpe: un poco de Poder y nada más
        dropLoot(c, 0, 0, random01() < 0.35f ? 1 : 0, ItemId::Potion, false, d.shadow);
        return;
    }

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
    int mpOrbs = random01() < 0.75f ? 1 + int(random01() * 2.0f) : 0;  // Poder (dorado o violeta según la familia)
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
            source->knock = -dir * (perfectGuard_ ? 2.5f : 3.0f);
        }
        if (perfectGuard_ && source) {  // aturdido: ventana para el súper contraataque
            source->stun = source->boss ? kParryStun * 0.45f : kParryStun;
            riposteWindow_ = kRiposteWindow;
            riposteTarget_ = int(source - enemies_.data());
            hitStop_ = 0.16f;
            shake_ = std::max(shake_, 0.3f);
            spawnRing(hero_.body.pos + Vec3{0, 0.05f, 0}, 3.0f, 0.35f, rgba(160, 230, 255));
            notify("¡Bloqueo perfecto! · ataca: contraataque", rgba(160, 230, 255));
        } else if (perfectGuard_) {
            hitStop_ = 0.08f;
        }
        sound(perfectGuard_ ? Sfx::PerfectGuard : Sfx::Guard, chest);
        if (source) source->rlReward -= perfectGuard_ ? 1.0f : 0.5f;  // bloqueado: mala táctica
        return;
    }
    if (form_ == Form::Wolf) {  // el lobo apenas se inmuta: menos daño y no se tambalea
        damage *= 0.7f;
        knock *= 0.3f;
    }
    hp_ = std::max(0.0f, hp_ - damage);
    voiceCooldown_ = 0;
    voice(VoiceLine::Hurt, damage >= 4.0f ? 1.0f : 0.6f);
    if (sandbox() && infHp_) hp_ = maxHp_;  // modo prueba: vida infinita
    if (source) source->rlReward += damage / 10.0f;  // le ha hecho daño: buena táctica
    sound(Sfx::HeroHurt, chest);
    heroInvuln_ = kInvulnAfterHit;
    heroFlash_ = 0.25f;
    shake_ = std::max(shake_, 0.25f);
    if (form_ != Form::Wolf) hero_.flinch(0.35f);
    hero_.push(dir * knock + Vec3{0, form_ == Form::Wolf ? 0.0f : 2.0f, 0});
    spawnSparks(chest, dir, 10, rgba(255, 90, 90), 3.0f);
    if (hp_ <= 0) defeatHero();
}

void Game::defeatHero() {
    hp_ = 0;
    inLight_ = travel_ = lightLock_ = diving_ = onWall_ = false;
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

// --- Súper contraataque --------------------------------------------------------------------

namespace {
const Move kRiposteBlade[] = {Move::Counter, Move::Slash1, Move::Slash2, Move::Slash3, Move::Finisher};
const Move kRiposteHammer[] = {Move::Sweep, Move::Smash, Move::Sweep, Move::Quake};
const Move kRiposteFists[] = {Move::Punch, Move::PunchL, Move::Punch, Move::PunchL, Move::Uppercut};
}  // namespace

int Game::riposteLength() const { return hero_.moveSet == MoveSet::Hammer ? 4 : 5; }

Move Game::riposteMove(int step) const {
    if (hero_.moveSet == MoveSet::Hammer) return kRiposteHammer[step];
    if (hero_.moveSet == MoveSet::Fists) return kRiposteFists[step];
    return kRiposteBlade[step];
}

void Game::updateRiposte(float dt) {
    riposteWindow_ = std::max(0.0f, riposteWindow_ - dt);
    auto target = [&]() -> Enemy* {
        if (riposteTarget_ < 0 || riposteTarget_ >= int(enemies_.size())) return nullptr;
        Enemy& e = enemies_[size_t(riposteTarget_)];
        return e.targetable() ? &e : nullptr;
    };
    Enemy* e = target();
    if (riposteStep_ < 0) {  // ¿empieza? Atacar con el aturdido a tiro
        if (riposteWindow_ <= 0 || attackBuffer_ <= 0 || !e || e->stun <= 0 || hero_.rolling() || hero_.hurt()) return;
        if (length(flat(e->body.pos - hero_.body.pos)) > 7.0f) return;
        riposteStep_ = 0;
        riposteWindow_ = 0;
        attackBuffer_ = 0;
        lock_ = riposteTarget_;
        hero_.setArmed(true, true);
        spawnRing(hero_.body.pos + Vec3{0, 0.05f, 0}, 2.0f, 0.3f, shadowMode_ ? kRiposteShadow : rgba(255, 220, 120));
        sound(Sfx::PerfectGuard, hero_.body.pos, 1.0f, 1.3f);
    }
    attackBuffer_ = 0;  // durante la ráfaga, el clic no cuenta
    if (!e || hero_.downTimer >= 0) {
        riposteStep_ = -1;
        return;
    }
    heroInvuln_ = std::max(heroInvuln_, 0.15f);
    e->stun = std::max(e->stun, e->stateTime + 0.6f);  // sigue aturdido mientras dura
    bool ready = hero_.move() == Move::None || (hero_.moveU() > 0.72f && !isFinisher(hero_.move()));
    if (!ready) return;
    if (riposteStep_ >= riposteLength()) {
        if (hero_.move() == Move::None) riposteStep_ = -1;  // terminado
        return;
    }
    Vec3 to = flat(e->body.pos - hero_.body.pos);
    float d = length(to), reach = inv_.weaponStats().reach;
    float yaw = d > 1e-3f ? std::atan2(to.x, to.z) : hero_.yaw;
    hero_.forceMove(riposteMove(riposteStep_), yaw, d > reach * 0.7f ? std::min(d - reach * 0.6f, 5.0f) : 0.0f);
    ++riposteStep_;
    // Estela de imágenes: chispas a lo largo del acercamiento.
    spawnSparks(hero_.body.pos + Vec3{0, 1, 0}, normalize(to), 8, shadowMode_ ? kRiposteShadow : rgba(255, 230, 160), 3.0f);
}

// --- Voz ------------------------------------------------------------------------------------

void Game::voice(VoiceLine line, float chance) {
    if (voiceCooldown_ > 0 || random01() > chance) return;
    bool dark = shadowMode_;
    Sfx id;
    switch (line) {
        case VoiceLine::Attack:
            id = dark ? (random01() < 0.5f ? Sfx::HollinAttack1 : Sfx::HollinAttack2)
                      : (random01() < 0.5f ? Sfx::VelaAttack1 : Sfx::VelaAttack2);
            break;
        case VoiceLine::Finisher: id = dark ? Sfx::HollinFinisher : Sfx::VelaFinisher; break;
        case VoiceLine::Jump: id = dark ? Sfx::HollinJump : Sfx::VelaJump; break;
        case VoiceLine::Hurt: id = dark ? Sfx::HollinHurt : Sfx::VelaHurt; break;
        case VoiceLine::Dodge: id = dark ? Sfx::HollinDodge : Sfx::VelaDodge; break;
        case VoiceLine::Laugh:
            if (!dark) return;  // Vela no se ríe de nadie
            id = Sfx::HollinLaugh;
            break;
        default: return;
    }
    // El remate, algo más grave y más fuerte (con voces grabadas sale de las mismas tomas).
    bool big = line == VoiceLine::Finisher;
    sound(id, hero_.body.pos, big ? 1.0f : 0.85f, big ? randRange(0.92f, 0.96f) : randRange(0.97f, 1.04f));
    voiceCooldown_ = line == VoiceLine::Attack ? 0.22f : 0.35f;
}
