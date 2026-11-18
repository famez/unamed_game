// Comportamiento de los enemigos: qué deciden (con su cerebro aprendido), cómo se mueven
// y atacan, y sus proyectiles.

#include "game/game.h"

#include <algorithm>

namespace {

constexpr int kMaxAttackers = 2;  // a la vez (sin contar jefes): los demás rodean y esperan

}  // namespace

// El Prismero tiene al enemigo delante (o fijado): no es buen momento para atacarle.
bool Game::heroWatches(const Enemy& e) const {
    if (lock_ >= 0 && &enemies_[size_t(lock_)] == &e) return true;
    Vec3 to = flat(e.body.pos - hero_.body.pos);
    float d = length(to);
    return d < 0.1f || dot(to * (1.0f / d), hero_.forward()) > 0.4f;
}

Brain& Game::brainFor(const Enemy& e) { return e.boss ? bossBrain_ : brains_[int(e.kind)]; }

// Qué está haciendo el Prismero, tal como lo ve un Gris.
int Game::heroActionCode() const {
    if (hero_.rolling() || inLight_ || travel_ || diving_) return 3;
    if (!hero_.body.grounded) return 4;
    if (hero_.guarding()) return 2;
    if (isAttack(hero_.move())) return 1;
    return 0;
}

// Cierra la decisión anterior (aprende de su recompensa) y elige la siguiente táctica.
void Game::decideIntent(Enemy& e, float dist) {
    Brain& b = brainFor(e);
    BrainInput in{dist, heroActionCode(), std::max(0.0f, e.health) / (e.def().maxHealth * (e.boss ? 7.0f : 1.0f)),
                  e.cooldown <= 0, heroWatches(e)};
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
    if (lightAt(e.body.pos) > 0.3f) slow *= d.shadow ? 0.8f : 1.1f;  // la luz pesa a los de sombra y aviva a los de luz
    e.dodgeCooldown = std::max(0.0f, e.dodgeCooldown - dt);
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
    if (isSwarm(e.kind)) {  // polillas y luciérnagas: swarm.cpp
        updateSwarm(e, dt);
        return;
    }

    Vec3 toHero = flat(hero_.body.pos - e.body.pos);
    float dist = length(toHero);
    Vec3 dirHero = dist > 1e-4f ? toHero * (1.0f / dist) : e.forward();
    // Dentro de la luz no le ven; fundido con la sombra, solo muy de cerca.
    bool heroOk = hero_.downTimer < 0 && !inLight_ && !travel_ && !diving_;
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
            bool watched = heroWatches(e);
            switch (e.intent) {
                case Intent::Approach: desired = approach; break;
                case Intent::Flank: {  // rodea hasta la espalda del Prismero y ataca desde ahí
                    Vec3 side{-hero_.forward().z, 0, hero_.forward().x};
                    Vec3 spot = hero_.body.pos - hero_.forward() * (contact + 0.8f) + side * (std::fmod(e.seed, 2.0f) < 1.0f ? 0.8f : -0.8f);
                    Vec3 to = flat(spot - e.body.pos);
                    desired = length(to) > 0.4f ? normalize(to) * (d.speed * 1.1f) : Vec3{};
                    attack = inRange && !watched;
                    break;
                }
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
            // Oportunistas: si les das la espalda, atacan aunque su táctica fuera otra.
            if (!watched && inRange && (e.kind == EnemyKind::Hound || e.kind == EnemyKind::Dazzled)) attack = true;
            // Turnos: como mucho dos atacan a la vez; el resto rodea, y así te encierran.
            if (attack && !e.boss) {
                int busy = 0;
                for (const Enemy& o : enemies_)
                    if (&o != &e && !o.boss && (o.state == AiState::Windup || o.state == AiState::Attack)) ++busy;
                if (busy >= kMaxAttackers) {
                    attack = false;
                    desired = (tangent * 0.9f + dirHero * (dist > 3.5f ? 0.3f : -0.3f)) * d.speed;
                }
            }
            if (attack) {
                e.state = AiState::Windup;
                e.stateTime = 0;
                e.attackStep = 0;
            }
            // Esquivar: si el Prismero lanza un golpe y le tiene delante, se aparta de un
            // salto (más a menudo cuanto más ha aprendido su especie).
            if (!attack && !d.flying && e.dodgeCooldown <= 0 && isAttack(hero_.move()) && dist < 3.0f && watched &&
                e.kind != EnemyKind::Brute) {
                e.dodgeCooldown = 1.2f;
                float skill = std::min(1.0f, float(brainFor(e).experience()) / 800.0f);
                float chance = (e.kind == EnemyKind::Hound ? 0.35f : 0.25f) + 0.3f * skill;
                if (random01() < chance) {
                    Vec3 side = tangent * (random01() < 0.5f ? 1.0f : -1.0f);
                    e.knock = side * 7.5f - dirHero * 3.0f;
                    e.body.vel.y = 3.0f;
                    e.rlReward += 0.2f;
                    sound(Sfx::Dodge, e.center(), 0.5f, 1.2f);
                }
            }
            break;
        }
        case AiState::Windup:
            faceHero = true;
            if (e.stateTime > d.windup) enemyStartAttack(e);
            else if (e.stateTime <= edt)
                sound(Sfx::EnemyWindup, e.center(), 0.6f,
                      e.kind == EnemyKind::Brute ? 0.7f : e.kind == EnemyKind::Hound ? 1.3f : e.kind == EnemyKind::Crab ? 0.85f : 1.0f);
            break;
        case AiState::Attack: {
            Vec3 heroChest = hero_.body.pos + Vec3{0, 0.9f, 0};
            if (e.kind == EnemyKind::Dazzled && e.attackStep == 0 && e.stateTime > 0.25f) {
                // Segundo zarpazo: otro impulso hacia el Prismero.
                e.attackStep = 1;
                if (!e.attackHit) e.rlReward -= 0.1f;
                e.attackHit = false;
                e.knock = dirHero * 6.5f;
                sound(Sfx::Slash, e.center(), 0.7f, 0.8f);
            }
            if (e.kind == EnemyKind::Dazzled || e.kind == EnemyKind::Hound || e.kind == EnemyKind::Crab) {
                Vec3 aim = e.kind == EnemyKind::Hound  ? e.center() + e.forward() * 0.5f
                           : e.kind == EnemyKind::Crab ? e.center() + e.forward() * (0.45f * e.size)  // las pinzas, por delante
                                                       : e.center();
                if (!e.attackHit && length(aim - heroChest) < 0.95f + e.body.radius) {
                    e.attackHit = true;
                    hurtHero(e.forward(), d.damage * (e.boss ? 1.6f : 1.0f), d.knock, &e);
                }
                if (e.stateTime > d.attackTime) {
                    if (!e.attackHit) e.rlReward -= 0.25f;  // ataque fallado
                    e.state = AiState::Recover;
                    e.stateTime = 0;
                }
            } else if (e.kind == EnemyKind::Brute) {
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
            if (e.stateTime > std::max(0.45f, e.stun)) {
                e.stun = 0;
                e.state = AiState::Chase;
                e.stateTime = 0;
                e.cooldown = std::max(e.cooldown, 0.3f);
            }
            break;
        case AiState::Dying: break;
    }

    // Orientación: hacia el jugador o hacia donde camina.
    if (e.kind == EnemyKind::Crab && e.state == AiState::Chase && dist > 3.2f) faceHero = false;  // de lado (abajo)
    if (faceHero) {
        float target = std::atan2(dirHero.x, dirHero.z);
        e.yaw = wrapAngle(e.yaw + clampf(wrapAngle(target - e.yaw), -8.0f * edt, 8.0f * edt));
    } else if (length(desired) > 0.1f && e.state == AiState::Idle) {
        float target = std::atan2(desired.x, desired.z);
        e.yaw = wrapAngle(e.yaw + clampf(wrapAngle(target - e.yaw), -4.0f * edt, 4.0f * edt));
    }
    // El cangrejo anda de lado: lejos, avanza con el costado por delante (y se le puede dar
    // por ahí); de cerca encara al Prismero con las pinzas y el caparazón.
    if (e.kind == EnemyKind::Crab && length(desired) > 0.1f && (e.state == AiState::Idle || (e.state == AiState::Chase && dist > 3.2f))) {
        float along = std::atan2(desired.x, desired.z);
        float a = wrapAngle(along + kPi * 0.5f), b = wrapAngle(along - kPi * 0.5f);
        float target = std::fabs(wrapAngle(a - e.yaw)) < std::fabs(wrapAngle(b - e.yaw)) ? a : b;
        e.yaw = wrapAngle(e.yaw + clampf(wrapAngle(target - e.yaw), -10.0f * edt, 10.0f * edt));
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
    animateEnemy(e, time_, dt);
    // Jefe: al bajar de 2/3 y de 1/3 de vida llama a su jauría. Va lo último: al añadir
    // enemigos el vector puede moverse en memoria y `e` deja de valer.
    if (e.boss && e.alive()) {
        float frac = e.health / (d.maxHealth * 7.0f);
        int phase = frac < 0.33f ? 2 : frac < 0.66f ? 1 : 0;
        if (phase > e.bossPhase) {
            e.bossPhase = phase;
            EnemyKind kind = d.shadow ? EnemyKind::Hound : EnemyKind::Dazzled;
            Vec3 at = e.body.pos;
            shake_ = std::max(shake_, 0.3f);
            for (float sx : {-1.0f, 1.0f}) spawnEnemy({kind, at + Vec3{sx * 3.0f, 0, -2.0f}});
        }
    }
}

void Game::enemyStartAttack(Enemy& e) {
    e.state = AiState::Attack;
    e.stateTime = 0;
    e.attackHit = false;
    Vec3 to = flat(hero_.body.pos - e.body.pos);
    float dist = length(to);
    switch (e.kind) {
        case EnemyKind::Dazzled:  // se lanza con el primer zarpazo
            e.knock = e.forward() * 8.0f;
            e.body.vel.y = 1.5f;
            sound(Sfx::Slash, e.center(), 0.7f, 0.7f);
            break;
        case EnemyKind::Hound:  // salto a la garganta
            e.knock = (dist > 1e-3f ? to * (1.0f / dist) : e.forward()) * std::min(11.0f, dist * 4.0f + 3.0f);
            e.body.vel.y = 3.2f;
            break;
        case EnemyKind::Brute:  // salta hacia el jugador para caerle encima
            e.body.vel.y = 7.5f;
            e.knock = (dist > 1e-3f ? to * (1.0f / dist) : e.forward()) * (std::min(dist, 3.5f) / 0.75f);
            break;
        case EnemyKind::Watcher: {  // orbe de luz que persigue un poco al jugador
            Vec3 origin = e.center() + e.forward() * 0.35f;
            Vec3 aim = normalize(hero_.body.pos + Vec3{0, 1.0f, 0} - origin);
            Projectile p{ProjectileKind::EnemyOrb, origin, aim * 10.0f, 4.0f};
            p.seed = random01() * 100.0f;
            projectiles_.push_back(p);
            spawnSparks(origin, aim, 8, rgba(255, 240, 200), 2.0f);
            break;
        }
        case EnemyKind::Crab:  // un paso corto al frente con el pinzazo
            e.knock = e.forward() * std::min(6.0f, dist * 3.0f);
            sound(Sfx::Slash, e.center(), 0.6f, 0.6f);
            break;
        case EnemyKind::Moth:
        case EnemyKind::Firefly:  // los enjambres atacan a su manera (swarm.cpp)
        case EnemyKind::Count: break;
    }
}

void Game::updateProjectiles(float dt) {
    Vec3 heroChest = hero_.body.pos + Vec3{0, 1.0f, 0};
    for (Projectile& p : projectiles_) {
        if (p.dead) continue;
        if ((p.life -= dt) <= 0) {
            if (p.kind == ProjectileKind::Fire) explodeFire(p.pos);
            if (p.kind == ProjectileKind::Burst) explodeBurst(p.pos);
            p.dead = true;
            continue;
        }
        if (p.kind == ProjectileKind::Burst) p.vel.y -= kGravity * dt;  // vuela en arco
        bool hostile = p.kind == ProjectileKind::EnemyOrb && !p.reflected;
        // Persecución suave del objetivo.
        float speed = length(p.vel);
        if (p.target >= int(enemies_.size()) || (p.target >= 0 && !enemies_[size_t(p.target)].targetable())) p.target = -1;
        Vec3 goal;
        float steer = 0;
        if (hostile && hero_.downTimer < 0) {
            goal = heroChest;
            steer = 1.6f;
        } else if (p.target >= 0) {
            goal = enemies_[size_t(p.target)].center();
            steer = 5.0f;
        }
        if (steer > 0) p.vel = normalize(p.vel + (normalize(goal - p.pos) * speed - p.vel) * std::min(1.0f, steer * dt)) * speed;

        Vec3 next = p.pos + p.vel * dt;
        if (p.kind == ProjectileKind::Spikes) {  // púas: siguen el suelo y lanzan a cada Gris una vez
            next.y = groundHeight(next + Vec3{0, 1.5f, 0}) + 0.9f;
            const WeaponStats& w = inv_.weaponStats();
            for (size_t i = 0; i < enemies_.size() && i < 32; ++i) {
                Enemy& e = enemies_[i];
                if (!e.targetable() || (p.hitMask & (1u << i)) || length(flat(e.body.pos - next)) > 1.3f + e.body.radius) continue;
                p.hitMask |= 1u << i;
                hitEnemy(e, normalize(flat(p.vel)), 8 + w.magic * 1.5f + (w.power + levelPower()) * 0.6f, 2.0f, 8.0f, true);
            }
            if (random01() < dt * 40.0f && particles_.size() < kMaxParticles)
                particles_.push_back({next - Vec3{0, 0.85f, 0}, {0, 2.5f, 0}, 0.5f, 0.5f, 0.1f, rgba(30, 16, 48), 0, false});
            p.pos = next;
            continue;
        }
        if (p.kind == ProjectileKind::Wave) {  // atraviesa: daña a cada Gris una sola vez
            for (size_t i = 0; i < enemies_.size() && i < 32; ++i) {
                Enemy& e = enemies_[i];
                if (!e.targetable() || (p.hitMask & (1u << i)) || length(flat(e.center() - next)) > 1.6f + e.body.radius ||
                    std::fabs(e.center().y - next.y) > 1.5f)
                    continue;
                p.hitMask |= 1u << i;
                hitEnemy(e, normalize(flat(p.vel)), (inv_.weaponStats().power + inv_.powerBonus + levelPower()) * 1.6f * formDamage(), 6.0f, 2.0f, true);
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
                hurtHero(normalize(flat(p.vel)), enemyDef(EnemyKind::Watcher).damage, enemyDef(EnemyKind::Watcher).knock,
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
                } else if (p.kind == ProjectileKind::Burst) {
                    explodeBurst(p.pos);
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
            else if (p.kind == ProjectileKind::Burst)
                explodeBurst(point + h.normal * 0.1f);
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

// Estallido solar: una explosión grande que lanza a todos por los aires y deja una bola de
// fuego volumétrico un instante.
void Game::explodeBurst(Vec3 point) {
    const float radius = 4.5f;
    const WeaponStats& w = inv_.weaponStats();
    Vec3 floor{point.x, groundHeight(point + Vec3{0, 0.5f, 0}) + 0.05f, point.z};
    sound(Sfx::FireBoom, point, 1.1f, 0.85f);
    spawnRing(floor, radius + 0.8f, 0.6f, rgba(255, 245, 210));
    spawnRing(floor, radius * 0.6f, 0.45f, rgba(255, 170, 60));
    spawnSparks(point, {0, 1, 0}, 40, rgba(255, 220, 140), 8.0f);
    blasts_.push_back({point, 0, 1.8f, random01() * 10.0f});
    shake_ = std::max(shake_, 0.45f);
    hitStop_ = std::max(hitStop_, 0.06f);
    for (Enemy& e : enemies_) {
        if (!e.targetable()) continue;
        Vec3 to = e.center() - point;
        float d = length(to);
        if (d > radius + e.body.radius) continue;
        float falloff = 1.0f - 0.5f * clampf(d / radius, 0, 1);
        Vec3 dir = normalize(flat(to));
        if (length(dir) < 0.5f) dir = hero_.forward();
        hitEnemy(e, dir, (14 + w.magic * 3.0f + (w.power + levelPower()) * 0.5f) * falloff, 9.0f, 6.0f, true);
    }
    for (Crate& c : crates_)
        if (!c.broken && length(c.box.center() - point) < radius) breakCrate(c);
    for (size_t i = 0; i < lamps_.size(); ++i)  // también revienta las farolas que pille
        if (!lamps_[i].broken && length(lamps_[i].pos - point) < radius * 0.8f) hitLamp(int(i), 3.0f);
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
