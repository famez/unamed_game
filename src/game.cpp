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
};
const SpellDef kSpells[int(Spell::Count)] = {
    {"Lanza de luz", 20, rgba(255, 230, 150)},
    {"Refracción", 15, rgba(120, 220, 255)},
    {"Calidez", 35, rgba(255, 160, 200)},
};
// clang-format on

constexpr float kHeroRespawn = 3.0f;
constexpr float kInvulnAfterHit = 0.8f;
constexpr float kPerfectGuard = 0.35f;  // bloquear al poco de levantar la guardia rechaza al atacante
constexpr float kHpOrb = 8, kMpOrb = 6;
constexpr size_t kMaxParticles = 900;

// El color que cada especie de Gris ha robado (y devuelve al disiparse).
uint32_t enemyColor(EnemyKind k) {
    switch (k) {
        case EnemyKind::Imp: return rgba(255, 70, 190);
        case EnemyKind::Shroom: return rgba(255, 185, 50);
        default: return rgba(40, 225, 255);
    }
}
constexpr float kCromaPerKill = 0.015f;

}  // namespace

const SpellDef& spellDef(Spell s) { return kSpells[int(s)]; }

float Game::random01() {
    rng_ = rng_ * 1664525u + 1013904223u;
    return float(rng_ >> 8) / float(1u << 24);
}

void Game::rebuildSolids() {
    world_.solids.clear();
    for (const Block& b : blocks_) world_.solids.push_back(b.box);
    for (const Crate& c : crates_)
        if (!c.broken) world_.solids.push_back(c.box);
    for (const TreasureChest& c : chests_) world_.solids.push_back(boxFromCenter(c.pos + Vec3{0, 0.35f, 0}, {0.45f, 0.35f, 0.45f}));
    for (const Gate& g : gates_)
        if (g.closed) world_.solids.push_back(g.box);
}

void Game::init(const char* assetDir) {
    // Historia: el guion del capítulo (sin él, el escenario no tiene combates ni escenas).
    names_ = {{"brizna", "???"}, {"prota", "???"}, {"voz", "Una voz"}, {"eco", "Eco"}, {"sistema", ""},
              {"archivera", "???"}, {"nora", "Nora"}};
    story_ = assetDir && script_.load(std::string(assetDir) + "/story/capitulo1.txt");
    bossBrain_.alpha = 0.45f;  // el jefe aprende muy deprisa durante el combate
    bossBrain_.epsilon = 0.1f;
    loadLevel(LevelId::Plaza);
    if (story_) {
        hero_.body.pos = {0, 0, -3.4f};  // despierta junto a la fuente
        hero_.yaw = kPi;
        hero_.lying = 1;
        playScene("intro");
    }
    inv_.add(ItemId::WoodenSword);
    inv_.add(ItemId::Potion, 3);
    inv_.add(ItemId::Ether, 1);
    applyWeapon();
}

void Game::applyWeapon() {
    const WeaponStats& w = inv_.weaponStats();
    hero_.comboHits = w.comboHits;
    hero_.attackSpeed = w.speed * (form_ == Form::Aurora ? 1.5f : form_ == Form::Colossus ? 0.85f : 1.0f);
    hero_.speedScale = form_ == Form::Aurora ? 1.3f : form_ == Form::Colossus ? 0.9f : 1.0f;
    hero_.gravityScale = form_ == Form::Echo ? 0.35f : 1.0f;
    hero_.infiniteJumps = form_ == Form::Echo;
    hero_.moveSet = w.style == WeaponStyle::Lance   ? MoveSet::Lance
                    : w.style == WeaponStyle::Fists ? MoveSet::Fists
                                                    : MoveSet::Blade;
}

// --- Bucle principal ---------------------------------------------------------------------

void Game::update(float dt, const Controls& c) {
    time_ += dt;
    updateStory(dt);
    // Escena que bloquea: el juego se detiene (salvo animaciones y efectos) hasta leerla.
    if (dialogue_.blocking()) {
        if (c.advance) dialogue_.advance();
        hero_.setGuard(false);
        hero_.update(dt, {}, false, world_);
        updateBrizna(dt);
        updateEffects(dt);
        return;
    }
    heroFlash_ = std::max(0.0f, heroFlash_ - dt);
    guardFlash_ = std::max(0.0f, guardFlash_ - dt);
    heroInvuln_ = std::max(0.0f, heroInvuln_ - dt);
    shake_ = std::max(0.0f, shake_ - dt * 1.5f);
    adaptNotice_ = std::max(0.0f, adaptNotice_ - dt);
    fovKick_ += ((hero_.dashAmount() > 0 ? 0.08f : 0.0f) + (isFinisher(hero_.move()) ? 0.06f : 0.0f) - fovKick_) *
                (1.0f - std::exp(-8.0f * dt));
    lastHitAge_ += dt;
    hpGhost_ = hpGhost_ > hp_ ? (heroFlash_ > 0 ? hpGhost_ : moveTowards(hpGhost_, hp_, 40.0f * dt)) : hp_;

    bool down = hero_.downTimer >= 0;
    if (down) {
        hero_.downTimer += dt;
        if (hero_.downTimer > kHeroRespawn) respawnHero();
    } else if (inLight_ || travel_) {  // hecho luz: solo se elige fuente, se salta o se sale
        updateLightTarget(c);
        if (inLight_) updateInsideLight(dt, c);
        lock_ = -1;
        if (c.advance && dialogue_.active()) dialogue_.advance();
    } else {
        updateLightTarget(c);
        updateLight(dt, c);
        updateAllies(dt, c);
        // R fija al Gris más centrado y, si ya hay uno fijado, lo suelta; el clic central
        // pasa al siguiente.
        if (c.lockOn) {
            if (lock_ >= 0) lock_ = -1;
            else cycleLock(c.camForward);
        }
        if (c.nextTarget) cycleLock(c.camForward);
        if (c.unlock) lock_ = -1;
        if (c.commandScroll != 0)
            commandSel_ = ((commandSel_ + c.commandScroll) % kCommandCount + kCommandCount) % kCommandCount;
        if (c.command >= 0 && c.command < kCommandCount) runCommand(c.command);
        else if (c.runCommand) runCommand(commandSel_);
        hero_.setGuard(c.guard);
        if (c.advance && dialogue_.active()) dialogue_.advance();
        if (c.dodge) dodgeBuffer_ = 0.15f;
        if (c.attack) attackBuffer_ = 0.25f;
        if (c.dodge || c.attack) bufferedMove_ = c.move;
        if (dodgeBuffer_ > 0 && hero_.dodge(length(bufferedMove_) > 0.1f ? bufferedMove_ : hero_.forward())) {
            sound(Sfx::Dodge, hero_.body.pos);
            dodgeBuffer_ = 0;
            attackBuffer_ = 0;
        }
        if (attackBuffer_ > 0 && heroAttack(length(c.move) > 0.1f ? c.move : bufferedMove_)) attackBuffer_ = 0;
        attackBuffer_ = std::max(0.0f, attackBuffer_ - dt);
        dodgeBuffer_ = std::max(0.0f, dodgeBuffer_ - dt);
        if (c.interact) tryInteract();
    }
    if (lock_ >= int(enemies_.size()) || (lock_ >= 0 && !enemies_[size_t(lock_)].targetable())) lock_ = -1;

    // Parón al golpear: todo se congela un instante salvo los efectos.
    if (hitStop_ > 0) {
        hitStop_ -= dt;
        updateEffects(dt);
        return;
    }

    bool wasGrounded = hero_.body.grounded;
    hero_.setSprint(c.sprint);
    hero_.setAttackHeld(c.attackHeld);
    bool light = inLight_ || travel_;
    hero_.update(dt, down || light ? Vec3{} : c.move, !down && !light && c.jump, world_, std::atan2(c.camForward.x, c.camForward.z));
    if (c.jump && wasGrounded && hero_.body.vel.y > 1.0f) sound(Sfx::Jump, hero_.body.pos, 0.7f);
    updateTravel(dt);
    updateBrizna(dt);
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

    cromaShown_ = moveTowards(cromaShown_, croma_, 0.25f * dt);
    for (ColorSplash& sp : splashes_) sp.age += dt;
    updateLevel(dt);
    updateEffects(dt);
    if (testMode_ != TestMode::None) {  // modo prueba: vida y Luz infinitas
        hp_ = maxHp_;
        mp_ = maxMp_;
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
        hitEnemy(e, dir, (w.power + inv_.powerBonus) * hp.damage * formDamage(), hp.knock * (form_ == Form::Colossus ? 1.4f : 1.0f),
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
    Sfx swing = m == Move::Finisher ? Sfx::Hammer : (m == Move::Slash3 || m == Move::AirFinisher) ? Sfx::Whip : Sfx::Slash;
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
                hitEnemy(e, normalize(flat(e.body.pos - at)), (w.power + inv_.powerBonus) * 0.5f, 4.0f, 2.0f, true);
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
}

void Game::hitEnemy(Enemy& e, Vec3 dir, float damage, float knock, float lift, bool heavy) {
    if (lightAt(e.body.pos) > 0.3f) damage *= 1.5f;  // en la luz, los Grises se debilitan
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
    bool armored = (e.kind == EnemyKind::Shroom && e.state == AiState::Attack && !heavy) || (e.boss && !heavy);
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
    splashes_.push_back({ground, randRange(1.1f, 1.6f), col, 0});
    if (splashes_.size() > size_t(Renderer::kMaxPaints)) splashes_.erase(splashes_.begin());
    for (int i = 0; i < 14 && particles_.size() < kMaxParticles; ++i) {
        float a = random01() * 2.0f * kPi;
        particles_.push_back({c, {std::cos(a) * randRange(2, 5), randRange(2, 5), std::sin(a) * randRange(2, 5)}, 0.7f, 0.7f,
                              0.09f, col, 9.8f, false});
    }
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
    if (hero_.downTimer >= 0 || hero_.rolling() || inLight_ || travel_ || heroInvuln_ > 0) return;
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
    if (testMode_ != TestMode::None) hp_ = maxHp_;  // modo prueba: vida infinita
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
    inLight_ = travel_ = lightLock_ = false;
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

// --- Enemigos ------------------------------------------------------------------------

Brain& Game::brainFor(const Enemy& e) { return e.boss ? bossBrain_ : brains_[int(e.kind)]; }

// Qué está haciendo el Prismero, tal como lo ve un Gris.
int Game::heroActionCode() const {
    if (hero_.rolling() || inLight_ || travel_) return 3;
    if (!hero_.body.grounded) return 4;
    if (hero_.guarding()) return 2;
    if (isAttack(hero_.move())) return 1;
    return 0;
}

// Cierra la decisión anterior (aprende de su recompensa) y elige la siguiente táctica.
void Game::decideIntent(Enemy& e, float dist) {
    Brain& b = brainFor(e);
    BrainInput in{dist, heroActionCode(), std::max(0.0f, e.health) / (e.def().maxHealth * (e.boss ? 7.0f : 1.0f)),
                  e.cooldown <= 0};
    int s = Brain::state(in);
    if (e.rlState >= 0) {
        Intent before = b.best(e.rlState);
        b.learn(e.rlState, e.intent, e.rlReward - 0.01f, s, false);
        // Un jefe que cambia de idea en voz alta: se nota que se adapta.
        if (e.boss && b.best(e.rlState) != before && adaptNotice_ <= 0) {
            notify(std::string(e.name()) + " aprende de tus movimientos", rgba(255, 170, 90));
            adaptNotice_ = 15.0f;
        }
    }
    e.rlReward = 0;
    e.rlState = s;
    e.intent = b.choose(s, random01(), random01());
    e.decisionTimer = 0.5f;
    e.feintTimer = 0.4f;
}

void Game::updateEnemy(Enemy& e, float dt) {
    const EnemyDef& d = e.def();
    for (int i = 0; i < d.jointCount; ++i) e.prevJoints[i] = e.joints[i];
    e.prevPos = e.body.pos;
    float slow = e.freeze > 0 ? 0.45f : 1.0f;  // congelado: todo más lento
    if (lightAt(e.body.pos) > 0.3f) slow *= 0.8f;  // la luz les pesa
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
    bool heroOk = hero_.downTimer < 0 && !inLight_ && !travel_;  // dentro de la luz no le ven
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
            // La táctica la decide su cerebro (aprendizaje por refuerzo) cada medio segundo.
            e.decisionTimer -= edt;
            if (e.decisionTimer <= 0) decideIntent(e, dist);
            Vec3 tangent{-dirHero.z, 0, dirHero.x};
            if (std::fmod(e.seed, 2.0f) < 1.0f) tangent = -tangent;
            float dy = std::fabs(hero_.body.pos.y - e.body.pos.y);
            bool inRange = dist < d.attackRange * e.size && e.cooldown <= 0 && (d.flying || dy < 1.2f);
            float contact = e.body.radius + hero_.body.radius + 0.25f;
            Vec3 approach = d.flying ? (dirHero * (dist > 6.5f ? 1.0f : dist < 4.0f ? -1.0f : 0.0f) + tangent * 0.7f) * d.speed
                            : dist > contact ? dirHero * d.speed : Vec3{};
            bool attack = false;
            switch (e.intent) {
                case Intent::Approach: desired = approach; break;
                case Intent::Attack:
                    desired = approach;
                    attack = inRange;
                    break;
                case Intent::Retreat: desired = -dirHero * (d.speed * 0.8f); break;
                case Intent::Circle:
                    desired = (tangent * 0.85f + dirHero * (dist > 4.5f ? 0.35f : dist < 2.0f ? -0.35f : 0.0f)) * d.speed;
                    break;
                case Intent::Wait: break;
                case Intent::Feint:  // amago: se queda quieto un momento (a ver si esquivas) y luego ataca
                    if (e.feintTimer > 0) {
                        e.feintTimer -= edt;
                        desired = -dirHero * (d.speed * 0.15f);
                    } else {
                        desired = approach;
                        attack = inRange;
                    }
                    break;
                case Intent::Count: break;
            }
            if (attack) {
                e.state = AiState::Windup;
                e.stateTime = 0;
            }
            break;
        }
        case AiState::Windup:
            faceHero = true;
            if (e.stateTime > d.windup) enemyStartAttack(e);
            else if (e.stateTime <= edt) sound(Sfx::EnemyWindup, e.center(), 0.6f, e.kind == EnemyKind::Shroom ? 0.7f : 1.0f);
            break;
        case AiState::Attack: {
            Vec3 heroChest = hero_.body.pos + Vec3{0, 0.9f, 0};
            if (e.kind == EnemyKind::Imp) {
                if (!e.attackHit && length(e.center() - heroChest) < 0.95f + e.body.radius) {
                    e.attackHit = true;
                    hurtHero(e.forward(), d.damage * (e.boss ? 1.6f : 1.0f), d.knock, &e);
                }
                if (e.stateTime > d.attackTime) {
                    if (!e.attackHit) e.rlReward -= 0.25f;  // ataque fallado
                    e.state = AiState::Recover;
                    e.stateTime = 0;
                }
            } else if (e.kind == EnemyKind::Shroom) {
                // Aterriza: onda alrededor. Saltar a tiempo la esquiva.
                if ((e.body.grounded && e.stateTime > 0.2f) || e.stateTime > 2.0f) {
                    const float radius = 2.6f * e.size;
                    spawnRing(e.body.pos + Vec3{0, 0.05f, 0}, radius, 0.45f, rgba(90, 240, 220));
                    spawnSparks(e.body.pos, {0, 1, 0}, 12, rgba(255, 230, 120), 4.0f, false);
                    shake_ = std::max(shake_, 0.2f);
                    bool heroLow = std::fabs(hero_.body.pos.y - e.body.pos.y) < 0.35f && hero_.body.grounded;
                    if (heroLow && dist < radius + hero_.body.radius)
                        hurtHero(dirHero, d.damage * (e.boss ? 1.6f : 1.0f), d.knock * (e.boss ? 1.4f : 1.0f), &e);
                    else
                        e.rlReward -= 0.25f;  // la onda no le ha alcanzado
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
        float targetY = groundHeight(e.body.pos + Vec3{0, 0.5f, 0}) + d.hoverHeight - e.body.height * 0.5f;
        if (e.state == AiState::Stagger) targetY -= 0.4f;
        if (e.state == AiState::Dying) targetY = e.body.pos.y;
        e.body.vel.y = (targetY - e.body.pos.y) * 3.0f + kGravity * dt;
    }
    moveBody(e.body, world_, dt, 0.3f);
    // Jefe: al bajar de 2/3 y de 1/3 de vida llama a dos Rastreros.
    if (e.boss && e.alive()) {
        float frac = e.health / (d.maxHealth * 7.0f);
        int phase = frac < 0.33f ? 2 : frac < 0.66f ? 1 : 0;
        if (phase > e.bossPhase) {
            e.bossPhase = phase;
            for (float sx : {-1.0f, 1.0f}) spawnEnemy({EnemyKind::Imp, e.body.pos + Vec3{sx * 3.0f, 0, -2.0f}});
            shake_ = std::max(shake_, 0.3f);
        }
    }
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
        if (p.kind == ProjectileKind::Wave) {  // atraviesa: daña a cada Gris una sola vez
            for (size_t i = 0; i < enemies_.size() && i < 32; ++i) {
                Enemy& e = enemies_[i];
                if (!e.targetable() || (p.hitMask & (1u << i)) || length(flat(e.center() - next)) > 1.6f + e.body.radius ||
                    std::fabs(e.center().y - next.y) > 1.5f)
                    continue;
                p.hitMask |= 1u << i;
                hitEnemy(e, normalize(flat(p.vel)), (inv_.weaponStats().power + inv_.powerBonus) * 1.6f * formDamage(), 6.0f, 2.0f, true);
            }
            p.pos = next;
            continue;
        }
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
    sound(Sfx::FireBoom, point, 0.8f, 1.3f);
    spawnRing(Vec3{point.x, groundHeight(point) + 0.06f, point.z}, 2.8f, 0.5f, rgba(255, 245, 200));
    spawnSparks(point, {0, 1, 0}, 22, rgba(255, 240, 180), 5.0f);
    spawnRing(Vec3{point.x, groundHeight(point) + 0.05f, point.z}, radius, 0.4f, rgba(255, 220, 140));
    shake_ = std::max(shake_, 0.15f);
    for (Enemy& e : enemies_) {
        if (!e.targetable()) continue;
        Vec3 to = e.center() - point;
        float d = length(to);
        if (d > radius + e.body.radius) continue;
        float falloff = 1.0f - 0.5f * clampf(d / radius, 0, 1);
        Vec3 dir = normalize(flat(to));
        if (length(dir) < 0.5f) dir = hero_.forward();
        hitEnemy(e, dir, (12 + magic * 2.0f) * falloff, 5.0f, 2.5f, true);
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
        case PickupKind::Hp: heal(kHpOrb, 0); col = rgba(120, 255, 140); uiSound(Sfx::Pickup, 0.5f, 1.0f); break;
        case PickupKind::Mp: heal(0, kMpOrb); col = rgba(120, 170, 255); uiSound(Sfx::Pickup, 0.5f, 1.25f); break;
        case PickupKind::Munny:
            inv_.munny += p.value;
            col = rgba(255, 220, 80);
            uiSound(Sfx::Coin, 0.45f, 0.9f + 0.03f * float(kills_ % 8));
            break;
        case PickupKind::Item:
            uiSound(Sfx::ItemGet, 0.8f);
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

bool Game::craft(const Recipe& r) {
    if (!inv_.craft(r)) return false;
    const ItemDef& d = itemDef(r.result);
    notify(std::string("Fabricado: ") + d.name + (r.count > 1 ? " ×" + std::to_string(r.count) : ""), d.color);
    uiSound(Sfx::ItemGet, 0.8f, 1.1f);
    return true;
}

bool Game::useItem(ItemId id) {
    const ItemDef& d = itemDef(id);
    if (d.kind == ItemKind::Core && inv_.count(id) > 0)  // núcleo: transformarse
        return startForm(id == ItemId::CoreAurora ? Form::Aurora : id == ItemId::CoreColossus ? Form::Colossus : Form::Echo);
    if (d.kind == ItemKind::Key && id == ItemId::NoraLantern) return summon(1);
    if (d.kind == ItemKind::Upgrade && inv_.count(id) > 0) {  // afilador: fuerza permanente
        if (inv_.powerBonus >= Inventory::kMaxPowerBonus) {
            notify("El prisma ya no admite más filo", rgba(180, 180, 190));
            return false;
        }
        inv_.remove(id);
        inv_.powerBonus = std::min(Inventory::kMaxPowerBonus, inv_.powerBonus + 2.0f);
        notify("Fuerza +2 (filo " + std::to_string(int(inv_.powerBonus)) + "/6)", rgba(255, 255, 255));
        uiSound(Sfx::PerfectGuard, 0.7f);
        return true;
    }
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

void Game::breakCrate(Crate& c) {
    c.broken = true;
    sound(Sfx::Crate, c.box.center());
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
