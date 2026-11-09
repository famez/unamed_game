#include "game.h"

#include <algorithm>

namespace {

// Parámetros de combate: daño, empuje horizontal (m/s) y empuje vertical (m/s).
struct HitParams {
    float damage, knock, lift;
};
constexpr HitParams kPunch{8, 3.5f, 1.0f};
constexpr HitParams kSlash{25, 6.0f, 2.5f};
constexpr HitParams kBullet{12, 2.5f, 0.5f};
constexpr HitParams kRock{15, 4.5f, 2.0f};

constexpr float kBulletSpeed = 45.0f;
// Lanzamiento: con la cámara en su inclinación por defecto sale tenso (bueno a corta
// distancia); mirando hacia arriba se lanza más alto y más lejos.
constexpr float kThrowSpeed = 13.0f, kThrowUp = 1.5f;
constexpr float kDefaultCameraPitch = 0.35f, kThrowUpPerRadian = 8.0f, kMaxThrowUp = 9.0f;
constexpr float kBlockFactor = 0.2f;    // daño que pasa a través del escudo
constexpr float kRespawnTime = 4.0f;

// Cóctel molotov.
constexpr float kBlastRadius = 3.0f, kBlastDamage = 25.0f, kBlastKnock = 5.0f, kBlastLift = 3.0f;
constexpr float kBurnDps = 20.0f;        // daño por segundo mientras arde
constexpr float kBurnAfterFire = 2.5f;   // sigue ardiendo este tiempo al salir del fuego
constexpr int kFireTongues = 22;
constexpr size_t kMaxFires = 6, kMaxScorches = 8;

void animateEnemy(Enemy& e, float time) {
    e.pose = Pose{};
    Vec3* r = e.pose.rotation;

    r[Chest].x = 0.02f * std::sin(time * 2.0f + e.breathOffset);
    // Brazo izquierdo sujetando el escudo delante del pecho; piernas algo abiertas.
    r[ShoulderL].x = -1.0f;
    r[ShoulderL].z = 0.35f;
    r[ElbowL].x = -1.2f;
    r[ShoulderR].z = -0.12f;
    r[ElbowR].x = -0.25f;
    r[HipL].z = 0.06f;
    r[HipR].z = -0.06f;

    // Tambaleo: se dobla en la dirección del empujón y se le doblan las rodillas.
    float s = e.hitTimer / 0.4f;
    s *= s;
    float lean = e.hitFromFront ? -1.0f : 1.0f;
    r[Spine].x += 0.5f * s * lean;
    r[Chest].x += 0.3f * s * lean;
    r[Neck].x += 0.3f * s * lean;
    r[ShoulderR].z -= 0.9f * s;
    r[ShoulderR].x += 0.6f * s * lean;
    r[KneeL].x += 0.3f * s;
    r[KneeR].x += 0.3f * s;
}

}  // namespace

const char* itemName(Item item) {
    switch (item) {
        case Item::Fists: return "Punos";
        case Item::Sword: return "Espada";
        case Item::Pistol: return "Pistola";
        case Item::Rocks: return "Piedras";
        case Item::Molotov: return "Molotov";
        default: return "?";
    }
}

Mat4 Enemy::modelMatrix() const {
    Mat4 m = translate(body.pos) * rotateY(yaw);
    if (!alive()) {  // cae de espaldas pivotando sobre los pies
        float t = smoothstep01(deadTimer / 0.6f);
        m = m * rotateX(-t * kPi * 0.5f);
    }
    return m;
}

float Game::random01() {
    rng_ = rng_ * 1664525u + 1013904223u;
    return float(rng_ >> 8) / float(1u << 24);
}

void Game::addEnemy(Vec3 pos, float yaw) {
    Enemy e;
    e.body.pos = e.spawn = pos;
    e.body.radius = 0.35f;
    e.yaw = e.spawnYaw = yaw;
    e.breathOffset = random01() * 6.0f;
    enemies_.push_back(e);
}

void Game::init() {
    auto box = [&](Vec3 c, Vec3 half) { world_.solids.push_back(boxFromCenter(c, half)); };

    // Escalera de bloques (hay que saltar cada escalón) hasta una plataforma alta.
    box({4.0f, 0.25f, 4.0f}, {0.75f, 0.25f, 0.75f});
    box({5.6f, 0.50f, 4.0f}, {0.75f, 0.50f, 0.75f});
    box({7.2f, 0.75f, 4.0f}, {0.75f, 0.75f, 0.75f});
    box({10.5f, 1.0f, 4.0f}, {2.5f, 1.0f, 2.5f});
    // Plataformas flotantes encadenadas; la última queda encima de un enemigo.
    box({-6.0f, 0.35f, 2.0f}, {0.8f, 0.35f, 0.8f});
    box({-4.0f, 1.45f, 4.2f}, {1.0f, 0.15f, 1.0f});
    box({-1.5f, 2.50f, 6.5f}, {1.0f, 0.15f, 1.0f});
    // Muro bajo y una escalinata suave (escalones de 0,25 m: se suben andando).
    box({0.0f, 0.5f, -6.0f}, {3.0f, 0.5f, 0.25f});
    for (int i = 0; i < 4; ++i)
        box({-8.0f, 0.125f * float(i + 1), -3.0f - 0.6f * float(i)}, {1.0f, 0.125f * float(i + 1), 0.3f});
    // Columnas en la periferia.
    for (int i = 0; i < 16; ++i) {
        float angle = random01() * 2.0f * kPi;
        float dist = 16.0f + random01() * 20.0f;
        float h = 1.0f + random01() * 4.0f, w = 0.3f + random01() * 1.0f;
        box({std::cos(angle) * dist, h * 0.5f, std::sin(angle) * dist}, {w, h * 0.5f, w});
    }

    addEnemy({0, 0, 6}, kPi);                 // de frente al jugador: hay que rodearlo
    addEnemy({-5, 0, 9}, kPi * 0.5f);         // mirando a +X
    addEnemy({10.5f, 2.0f, 4}, -kPi * 0.5f);  // en la plataforma alta, mirando la escalera
    addEnemy({4, 0, -4}, kPi);                // de espaldas al jugador
}

void Game::resetHero() {
    hero_.reset();
    rocks_ = 10;
    molotovs_ = 5;
}

void Game::update(float dt, const Controls& c) {
    time_ += dt;

    if (c.selectSlot >= 0 && c.selectSlot < int(Item::Count)) hero_.item = Item(c.selectSlot);

    // Apuntado con botón derecho (solo con la pistola).
    bool aiming = c.aim && hero_.item == Item::Pistol;
    if (aiming) updateAimPoint(c.aimOrigin, c.aimDir);
    hero_.setAim(aiming, aimPoint_);
    if (c.use)
        useItem(hero_.item, c.aimYaw, c.aimPitch);
    else if (c.throwRock)
        useItem(Item::Rocks, c.aimYaw, c.aimPitch);

    hero_.update(dt, c.move, c.run, c.jump, world_);
    Action impact = hero_.takeImpact();
    if (impact != Action::None) resolveImpact(impact);

    // Los enemigos vivos son sólidos para el jugador (separación de cilindros).
    for (const Enemy& e : enemies_) {
        if (!e.alive()) continue;
        Body& h = hero_.body;
        bool overlapY = h.pos.y < e.body.pos.y + e.body.height && h.pos.y + h.height > e.body.pos.y;
        Vec3 d = h.pos - e.body.pos;
        d.y = 0;
        float dist = length(d), minDist = h.radius + e.body.radius;
        if (overlapY && dist < minDist) h.pos += (dist > 1e-4f ? d * (1.0f / dist) : Vec3{1, 0, 0}) * (minDist - dist);
    }

    for (Enemy& e : enemies_) updateEnemy(e, dt);
    updateProjectiles(dt);
    updateFires(dt);
    updateParticles(dt);
}

void Game::useItem(Item item, float aimYaw, float aimPitch) {
    Action a = Action::None;
    switch (item) {
        case Item::Fists: a = Action::Punch; break;
        case Item::Sword: a = Action::Slash; break;
        case Item::Pistol: a = Action::Shoot; break;
        case Item::Rocks: a = rocks_ > 0 ? Action::Throw : Action::None; break;
        case Item::Molotov: a = molotovs_ > 0 ? Action::Throw : Action::None; break;
        default: break;
    }
    if (hero_.startAction(a, aimYaw) && a == Action::Throw) {
        throwUp_ = clampf(kThrowUp + (kDefaultCameraPitch - aimPitch) * kThrowUpPerRadian, 0, kMaxThrowUp);
        throwItem_ = item;
    }
}

void Game::resolveImpact(Action a) {
    Vec3 fwd = hero_.forward();
    switch (a) {
        case Action::Punch: meleeHit(1.0f, 0.6f, kPunch.damage, kPunch.knock, kPunch.lift); break;
        case Action::Slash: meleeHit(1.8f, 0.3f, kSlash.damage, kSlash.knock, kSlash.lift); break;
        case Action::Shoot: {
            // Del cañón al punto al que apunta el brazo: el de la mira o, desde la
            // cadera, uno lejano al frente.
            Vec3 muzzle = transformPoint(hero_.joints[WristR], kPistolMuzzle);
            Vec3 dir = normalize(hero_.aimTarget() - muzzle);
            projectiles_.push_back({ProjectileKind::Bullet, muzzle, dir * kBulletSpeed, 1.0f});
            spawnSparks(muzzle, fwd, 4, rgba(255, 220, 120), 2.0f);  // fogonazo
            break;
        }
        case Action::Throw: {
            bool molotov = throwItem_ == Item::Molotov;
            int& ammo = molotov ? molotovs_ : rocks_;
            if (ammo <= 0) break;
            --ammo;
            Vec3 hand = transformPoint(hero_.joints[WristR], kRockInHand);
            Vec3 vel = fwd * kThrowSpeed + Vec3{0, throwUp_, 0} + Vec3{hero_.body.vel.x, 0, hero_.body.vel.z};
            projectiles_.push_back({molotov ? ProjectileKind::Molotov : ProjectileKind::Rock, hand, vel});
            break;
        }
        default: break;
    }
}

// Golpe cuerpo a cuerpo: afecta a los enemigos dentro de un sector circular delante.
void Game::meleeHit(float range, float cosHalfArc, float damage, float knock, float lift) {
    Vec3 origin = hero_.body.pos, fwd = hero_.forward();
    for (Enemy& e : enemies_) {
        if (!e.alive()) continue;
        Vec3 d = e.body.pos - origin;
        float dy = d.y;
        d.y = 0;
        float dist = length(d);
        if (std::fabs(dy) > 1.2f || dist > range + e.body.radius) continue;
        Vec3 dir = dist > 1e-4f ? d * (1.0f / dist) : fwd;
        if (dot(dir, fwd) < cosHalfArc) continue;
        hitEnemy(e, dir, translationOf(e.joints[Chest]), damage, knock, lift);
    }
}

// `dir`: dirección horizontal en la que viaja el golpe (del atacante hacia el enemigo).
void Game::hitEnemy(Enemy& e, Vec3 dir, Vec3 point, float damage, float knock, float lift) {
    float facing = dot(dir, e.forward());
    bool blocked = facing < -0.35f;  // viene de frente: lo para el escudo
    if (blocked) {
        damage *= kBlockFactor;
        knock *= 0.5f;
        lift *= 0.5f;
        e.blockTimer = 0.25f;
        spawnSparks(transformPoint(e.joints[Chest], kShieldCenter), -dir, 14, rgba(255, 230, 120), 4.0f);
    } else {
        spawnSparks(point, dir, 10, rgba(255, 60, 60), 3.0f);
    }

    e.body.vel += dir * knock;
    e.body.vel.y += lift;
    e.hitTimer = 0.4f;
    e.hitFromFront = facing < 0;
    damageEnemy(e, damage);
}

void Game::damageEnemy(Enemy& e, float damage) {
    if (!e.alive()) return;
    e.health = std::max(0.0f, e.health - damage);
    if (e.health <= 0) {
        e.deadTimer = 0;
        e.burnTimer = 0;
        ++kills_;
    }
}

Item Game::handItem() const {
    if (hero_.action() == Action::Throw) return hero_.actionImpactDone() ? Item::Count : throwItem_;
    if (hero_.item == Item::Rocks && rocks_ == 0) return Item::Count;
    if (hero_.item == Item::Molotov && molotovs_ == 0) return Item::Count;
    return hero_.item;
}

// Rotura del cóctel: explosión de área (el escudo no protege: el fuego lo envuelve) y
// un charco de fuego sobre la superficie que haya debajo del impacto.
void Game::igniteAt(Vec3 point, Vec3 normal) {
    // Si dio en una pared o en un enemigo, se separa un poco y se busca el suelo debajo.
    Vec3 from = point + normal * 0.3f + Vec3{0, 0.3f, 0};
    // Si ese punto cae dentro de un sólido (rincón junto a un escalón), se parte de su techo.
    for (const AABB& b : world_.solids)
        if (from.x > b.min.x && from.x < b.max.x && from.z > b.min.z && from.z < b.max.z && from.y > b.min.y &&
            from.y < b.max.y)
            from.y = b.max.y + 0.05f;
    Vec3 surface{from.x, 0, from.z};
    RayHit h;
    if (segmentVsWorld(from, from - Vec3{0, 40, 0}, world_, h)) surface = from + Vec3{0, -40, 0} * h.t;

    Fire f;
    f.pos = surface;
    f.seed = random01() * 100.0f;
    // Lenguas repartidas en espiral (ángulo áureo) y solo donde hay suelo a la misma altura.
    for (int i = 0; i < kFireTongues; ++i) {
        float r = f.maxRadius * std::sqrt((float(i) + 0.5f) / float(kFireTongues));
        float a = float(i) * 2.39996f + f.seed;
        Vec3 p = surface + Vec3{std::cos(a) * r, 0, std::sin(a) * r};
        RayHit s;
        if (!segmentVsWorld(p + Vec3{0, 0.4f, 0}, p - Vec3{0, 0.6f, 0}, world_, s)) continue;
        p.y = p.y + 0.4f - s.t;
        if (std::fabs(p.y - surface.y) < 0.3f) f.tongues.push_back({p, r, random01() * 100.0f, 0.6f + random01() * 0.6f});
    }
    if (fires_.size() >= kMaxFires) fires_.erase(fires_.begin());
    fires_.push_back(f);
    blasts_.push_back({point, 0, random01() * 100.0f});
    if (scorches_.size() >= kMaxScorches) scorches_.erase(scorches_.begin());
    scorches_.push_back({surface, f.maxRadius * 1.1f, 0});

    for (Enemy& e : enemies_) {
        if (!e.alive()) continue;
        Vec3 center = e.body.pos + Vec3{0, 0.9f, 0};
        float d = length(center - point);
        if (d > kBlastRadius) continue;
        float falloff = 1.0f - 0.5f * d / kBlastRadius;
        Vec3 dir = normalize(Vec3{center.x - point.x, 0, center.z - point.z});
        if (length(dir) < 0.5f) dir = -e.forward();
        e.body.vel += dir * (kBlastKnock * falloff) + Vec3{0, kBlastLift * falloff, 0};
        e.hitTimer = 0.4f;
        e.hitFromFront = dot(dir, e.forward()) < 0;
        e.burnTimer = kBurnAfterFire;
        damageEnemy(e, kBlastDamage * falloff);
    }
    spawnSparks(point, {0, 1, 0}, 24, rgba(255, 170, 60), 6.0f);
    spawnSparks(point, normal, 12, rgba(140, 210, 160), 4.0f);  // cristales
}

void Game::updateFires(float dt) {
    for (Fire& f : fires_) {
        f.age += dt;
        float r = f.radius(), s = f.strength();
        if (s <= 0) continue;
        for (Enemy& e : enemies_) {
            if (!e.alive()) continue;
            Vec3 d = e.body.pos - f.pos;
            float dy = d.y;
            d.y = 0;
            if (dy > -0.6f && dy < 1.2f && length(d) < r * 0.85f + e.body.radius)
                e.burnTimer = std::max(e.burnTimer, kBurnAfterFire);
        }
        if (f.tongues.empty()) continue;
        const FireTongue& t = f.tongues[size_t(random01() * float(f.tongues.size())) % f.tongues.size()];
        if (t.dist > r) continue;
        if (random01() < dt * 8.0f * s)  // humo
            smoke_.push_back({t.pos + Vec3{0, 1.0f, 0}, {(random01() - 0.5f) * 0.4f, 1.1f + random01() * 0.6f, (random01() - 0.5f) * 0.4f},
                              0, 2.2f + random01(), random01() * 100.0f});
        if (random01() < dt * 16.0f * s && particles_.size() < 512)  // ascuas que suben
            particles_.push_back({t.pos + Vec3{0, 0.3f, 0}, {(random01() - 0.5f) * 0.8f, 1.5f + random01() * 2.0f, (random01() - 0.5f) * 0.8f},
                                  0.8f + random01() * 0.8f, rgba(255, 150, 50), -0.08f});
    }
    fires_.erase(std::remove_if(fires_.begin(), fires_.end(), [](const Fire& f) { return f.age > f.duration; }),
                 fires_.end());

    for (SmokePuff& p : smoke_) {
        p.age += dt;
        p.pos += p.vel * dt;
        p.vel.x += 0.3f * dt;  // algo de viento
    }
    smoke_.erase(std::remove_if(smoke_.begin(), smoke_.end(), [](const SmokePuff& p) { return p.age > p.life; }),
                 smoke_.end());
    for (Blast& b : blasts_) b.age += dt;
    blasts_.erase(std::remove_if(blasts_.begin(), blasts_.end(), [](const Blast& b) { return b.age > 0.6f; }),
                  blasts_.end());
    for (ScorchMark& m : scorches_) m.age += dt;
    scorches_.erase(std::remove_if(scorches_.begin(), scorches_.end(), [](const ScorchMark& m) { return m.age > 40; }),
                    scorches_.end());
}

void Game::updateEnemy(Enemy& e, float dt) {
    if (!e.alive()) {
        e.deadTimer += dt;
        if (e.deadTimer > kRespawnTime) {  // reaparece en su sitio con la vida llena
            Body fresh;
            fresh.pos = e.spawn;
            fresh.radius = e.body.radius;
            e.body = fresh;
            e.yaw = e.spawnYaw;
            e.health = e.maxHealth;
            e.deadTimer = -1;
            e.hitTimer = e.blockTimer = e.burnTimer = 0;
        }
    }

    // El retroceso se frena por rozamiento (mucho en el suelo, casi nada en el aire).
    float friction = std::exp(-(e.body.grounded ? 6.0f : 0.3f) * dt);
    e.body.vel.x *= friction;
    e.body.vel.z *= friction;
    moveBody(e.body, world_, dt, 0.0f);

    if (e.burnTimer > 0 && e.alive()) {
        damageEnemy(e, kBurnDps * dt);
        e.burnTimer = std::max(0.0f, e.burnTimer - dt);
        if (random01() < dt * 20.0f && particles_.size() < 512) {
            Vec3 p = e.body.pos + Vec3{(random01() - 0.5f) * 0.5f, 0.3f + random01() * 1.4f, (random01() - 0.5f) * 0.5f};
            particles_.push_back({p, {0, 1.5f + random01(), 0}, 0.6f + random01() * 0.5f, rgba(255, 150, 50), -0.08f});
        }
    }
    e.hitTimer = std::max(0.0f, e.hitTimer - dt);
    e.blockTimer = std::max(0.0f, e.blockTimer - dt);
    animateEnemy(e, time_);
    computeWorldMatrices(e.pose, e.modelMatrix(), e.joints);
}

void Game::updateProjectiles(float dt) {
    for (Projectile& p : projectiles_) {
        if (p.resting) {
            // Piedra en el suelo: se recoge al pasar por encima.
            Vec3 d = hero_.body.pos - p.pos;
            if (rocks_ < kMaxRocks && std::fabs(d.y) < 1.5f && d.x * d.x + d.z * d.z < 0.8f * 0.8f) {
                ++rocks_;
                p.dead = true;
            }
            continue;
        }
        bool bullet = p.kind == ProjectileKind::Bullet;
        bool molotov = p.kind == ProjectileKind::Molotov;
        if (molotov) p.spin += 12.0f * dt;
        if (bullet && (p.life -= dt) <= 0) {
            p.dead = true;
            continue;
        }
        if (!bullet) p.vel.y -= kGravity * dt;
        Vec3 next = p.pos + p.vel * dt;

        // Trazamos el segmento recorrido en este paso: a 45 m/s una bala avanza 0,75 m
        // por paso y atravesaría un enemigo si solo mirásemos el punto final.
        Enemy* target = nullptr;
        RayHit enemyHit;
        enemyHit.t = 2;
        if (p.armed)
            for (Enemy& e : enemies_) {
                RayHit h;
                if (e.alive() && segmentVsAABB(p.pos, next, e.hitBox(), h) && h.t < enemyHit.t) {
                    enemyHit = h;
                    target = &e;
                }
            }
        RayHit wallHit;
        bool hitWorld = segmentVsWorld(p.pos, next, world_, wallHit);

        if (target && (!hitWorld || enemyHit.t < wallHit.t)) {
            Vec3 point = p.pos + (next - p.pos) * enemyHit.t;
            Vec3 dir = normalize(Vec3{p.vel.x, 0, p.vel.z});
            if (molotov) {  // se rompe contra el enemigo
                igniteAt(point, -dir);
                p.dead = true;
                continue;
            }
            const HitParams& hp = bullet ? kBullet : kRock;
            hitEnemy(*target, dir, point, hp.damage, hp.knock, hp.lift);
            if (bullet) {
                p.dead = true;
            } else {  // la piedra rebota hacia atrás y deja de hacer daño
                p.pos = point - dir * 0.05f;
                p.vel = Vec3{-p.vel.x * 0.2f, 2.0f, -p.vel.z * 0.2f};
                p.armed = false;
            }
            continue;
        }
        if (hitWorld) {
            Vec3 point = p.pos + (next - p.pos) * wallHit.t;
            if (molotov) {
                igniteAt(point, wallHit.normal);
                p.dead = true;
                continue;
            }
            if (bullet) {
                spawnSparks(point, wallHit.normal, 5, rgba(200, 200, 200), 2.0f);
                p.dead = true;
                continue;
            }
            // Rebote con pérdida de energía y rozamiento.
            Vec3 n = wallHit.normal;
            p.vel = (p.vel - n * (1.35f * dot(p.vel, n))) * 0.7f;
            p.pos = point + n * 0.01f;
            if (length(p.vel) < 4.0f) p.armed = false;
            if (n.y > 0.7f && length(p.vel) < 1.5f) {
                p.resting = true;
                p.vel = {};
            }
            continue;
        }
        p.pos = next;
    }
    projectiles_.erase(std::remove_if(projectiles_.begin(), projectiles_.end(),
                                      [](const Projectile& p) { return p.dead; }),
                       projectiles_.end());
}

// Lanza el rayo de la cámara (por el centro de la pantalla) contra mundo y enemigos.
// No puede salir del cañón: la cámara va desplazada sobre el hombro y lo que importa es
// que la bala acabe donde el jugador ve la cruceta. Pero tampoco desde la propia cámara:
// lo que haya entre ella y el personaje (un enemigo a su espalda) daría un punto de
// impacto detrás de él. El rayo empieza donde cruza el plano del personaje.
void Game::updateAimPoint(Vec3 origin, Vec3 dir) {
    const float range = 120.0f;
    Vec3 chest = hero_.body.pos + Vec3{0, 1.4f, 0};
    origin = origin + dir * std::max(0.0f, dot(chest - origin, dir));
    Vec3 end = origin + dir * range;
    RayHit best;
    best.t = 1;
    aimOnEnemy_ = false;
    RayHit h;
    if (segmentVsWorld(origin, end, world_, h) && h.t < best.t) best = h;
    for (const Enemy& e : enemies_)
        if (e.alive() && segmentVsAABB(origin, end, e.hitBox(), h) && h.t < best.t) {
            best = h;
            aimOnEnemy_ = true;
        }
    aimPoint_ = origin + (end - origin) * best.t;
}

void Game::spawnSparks(Vec3 pos, Vec3 dir, int count, uint32_t color, float speed) {
    for (int i = 0; i < count && particles_.size() < 512; ++i) {
        Vec3 rnd{random01() - 0.5f, random01() - 0.2f, random01() - 0.5f};
        particles_.push_back({pos, dir * (speed * 0.6f) + rnd * (speed * 1.2f), 0.3f + random01() * 0.3f, color});
    }
}

void Game::updateParticles(float dt) {
    for (Particle& p : particles_) {
        p.vel.y -= 9.8f * p.gravity * dt;
        p.pos += p.vel * dt;
        if (p.pos.y < 0) {
            p.pos.y = 0;
            p.vel = p.vel * 0.3f;
        }
        p.life -= dt;
    }
    particles_.erase(std::remove_if(particles_.begin(), particles_.end(),
                                    [](const Particle& p) { return p.life <= 0; }),
                     particles_.end());
}
