// La sombra, la otra mitad del protagonista. Se cambia de luz a sombra (y al revés)
// cruzando un portal del escenario (o con Z en los niveles sueltos).
//   - La oscuridad es donde no llega la luz: lejos de las farolas encendidas (cada una alumbra
//     según la energía que le queda), de los nodos y de los faroles. Romper una farola a golpes
//     o apagarla con su interruptor crea oscuridad (rota ya no se arregla).
//   - Fundirse (Q en la oscuridad): el protagonista se hace sombra en el suelo y se mueve
//     por toda la zona oscura, rápido e invisible; la luz no le deja pasar. Gasta Poder.
//     Sale con Q o Espacio, o con el clic en emboscada sobre el enemigo más cercano.
//   - Técnicas de la sombra (combat.cpp): el agujero negro y la mina oscura se resuelven aquí.
//   - Golpes cruzados: la sombra hiere más a los de luz; la luz, a los de sombra.

#include "game/game.h"

#include <algorithm>

namespace {
constexpr float kSwapCooldown = 0.8f;
constexpr float kDiveCost = 6.0f;          // Poder por segundo fundido en la sombra
constexpr float kDiveSpeed = 1.7f;         // multiplicador de la carrera
constexpr float kHolePull = 7.0f, kBombPull = 4.0f;
const uint32_t kShadowCol = rgba(150, 80, 255), kShadowDark = rgba(30, 16, 48);
}  // namespace

// Radio que alumbra una farola según su energía.
float lampRadius(const Lamp& l) { return (2.5f + 4.0f * l.energy) * l.reach; }

float Game::lampLightAt(Vec3 p) const {
    float light = 0;
    for (const Lamp& l : lamps_) {
        if (!l.on || !l.bulb) continue;
        float dy = l.pos.y - p.y;
        if (dy < -1.0f || dy > 9.0f) continue;
        float r = lampRadius(l);
        light = std::max(light, 1.0f - smoothstep01((length(flat(p - l.pos)) - r * 0.6f) / (r * 0.4f)));
    }
    return light;
}

// Oscuridad: lo que no alumbra ninguna luz (farolas encendidas, nodos, faroles). Es lo mismo
// que oscurece el render (game_draw.cpp le pasa las mismas luces).
float Game::shadowAt(Vec3 p) const { return 1.0f - std::max(lightAt(p), lampLightAt(p)); }

// Multiplicador de daño según la mitad que usa el protagonista y la familia del enemigo
// (solo cuando ya tiene las dos: antes, la luz hiere a todos por igual).
float Game::familyDamage(const Enemy& e) const {
    if (!shadowUnlocked_) return 1.0f;
    return e.def().shadow == shadowMode_ ? 0.85f : 1.3f;
}

int Game::nearLamp(float range, bool lit) const {
    int best = -1;
    float bestD = range;
    for (size_t i = 0; i < lamps_.size(); ++i) {
        const Lamp& l = lamps_[i];
        if (!l.bulb || l.on != lit) continue;
        float d = length(flat(l.pos - hero_.body.pos)), dy = l.pos.y - hero_.body.pos.y;
        if (d < bestD && dy > -1.0f && dy < 10.0f) bestD = d, best = int(i);
    }
    return best;
}

// Gastar luz de una farola: se atenúa y, sin energía, se apaga.
void Game::drainLamp(int i, float amount) {
    if (i < 0 || i >= int(lamps_.size())) return;
    Lamp& l = lamps_[size_t(i)];
    l.energy = std::max(0.0f, l.energy - amount);
    if (l.energy <= 0.02f && l.on) {
        l.on = false;
        l.energy = 0;
        spawnSparks(l.pos, {0, -1, 0}, 14, rgba(120, 110, 140), 2.0f);
        sound(Sfx::Crate, l.pos, 0.5f, 0.6f);
    }
}

void Game::relightLamp(int i) {
    Lamp& l = lamps_[size_t(i)];
    if (l.broken) return;
    l.on = true;
    l.energy = 1.0f;
    Vec3 base{l.pos.x, groundHeight(l.pos + Vec3{0.6f, 0, 0}), l.pos.z};
    spawnSparks(l.pos, {0, -1, 0}, 24, l.color, 3.0f);
    spawnRing(base + Vec3{0, 0.05f, 0}, 4.0f, 0.6f, rgba(255, 226, 160));
    sound(Sfx::ItemGet, l.pos, 0.7f, 1.2f);
}

// --- Farolas: romperlas e interruptores ------------------------------------------------------

// Un golpe a una farola: aguanta unos pocos y se rompe para siempre (cristales, chispas y
// oscuridad alrededor). Es la forma de crear sombra donde no la hay.
void Game::hitLamp(int i, float damage) {
    Lamp& l = lamps_[size_t(i)];
    if (l.broken || !l.bulb) return;
    l.hp -= damage;
    l.hitFlash = 0.15f;
    spawnSparks(l.pos, {0, -1, 0}, 6, rgba(255, 240, 200), 3.0f);
    sound(Sfx::Hit, l.pos, 0.6f, 1.4f);
    if (l.hp > 0) return;
    l.broken = true;
    if (shadowMode_ && length(flat(l.pos - hero_.body.pos)) < 4.0f) voice(VoiceLine::Laugh, 0.7f);  // odia su zumbido
    l.on = false;
    l.energy = 0;
    if (hostLamp_ == i && inLight_) leaveLight(false);
    for (int k = 0; k < 18 && particles_.size() < kMaxParticles; ++k) {  // cristales que caen
        Vec3 v{randRange(-3.0f, 3.0f), randRange(0.5f, 3.0f), randRange(-3.0f, 3.0f)};
        particles_.push_back({l.pos, v, 0.9f, 0.9f, 0.05f, k % 3 ? rgba(220, 230, 240) : l.color, 9.0f, true});
    }
    spawnSparks(l.pos, {0, 1, 0}, 20, l.color, 5.0f);
    shake_ = std::max(shake_, 0.15f);
    sound(Sfx::Crate, l.pos, 1.0f, 1.5f);
    sound(Sfx::Ice, l.pos, 0.8f, 0.7f);
}

void Game::updateLamps(float dt) {
    for (Lamp& l : lamps_) l.hitFlash = std::max(0.0f, l.hitFlash - dt);
    for (LampSwitch& s : switches_) s.flip += dt;
    // Una farola rota suelta alguna chispa de vez en cuando.
    for (const Lamp& l : lamps_)
        if (l.broken && random01() < dt * 0.6f) spawnSparks(l.pos - Vec3{0, 0.15f, 0}, {0, -1, 0}, 3, rgba(255, 220, 160), 1.5f, false);
}

void Game::toggleSwitch(int i) {
    LampSwitch& s = switches_[size_t(i)];
    s.on = !s.on;
    s.flip = 0;
    for (int li : s.lamps) {
        Lamp& l = lamps_[size_t(li)];
        if (l.broken) continue;
        if (s.on) {
            relightLamp(li);
        } else {
            l.on = false;
            if (hostLamp_ == li && inLight_) leaveLight(false);
            spawnSparks(l.pos, {0, -1, 0}, 8, rgba(120, 110, 140), 2.0f);
        }
    }
    sound(Sfx::Chest, s.pos, 0.6f, s.on ? 1.4f : 0.8f);
    notify(s.on ? "Farolas encendidas" : "Farolas apagadas: ahí ahora hay sombra", s.on ? rgba(255, 226, 160) : kShadowCol);
}

// Interruptores: las farolas se agrupan por cercanía (hasta 3 por grupo) y cada grupo tiene
// el suyo, apartado: a 7-10 m del grupo, en un sitio libre y lejos de la salida del héroe.
void Game::placeLampSwitches() {
    switches_.clear();
    std::vector<bool> used(lamps_.size(), false);
    for (size_t i = 0; i < lamps_.size(); ++i) {
        if (used[i] || !lamps_[i].bulb) continue;
        LampSwitch s;
        Vec3 c;
        for (size_t k = i; k < lamps_.size() && s.lamps.size() < 3; ++k)
            if (!used[k] && lamps_[k].bulb && length(flat(lamps_[k].pos - lamps_[i].pos)) < 14.0f) {
                used[k] = true;
                s.lamps.push_back(int(k));
                c += lamps_[k].pos;
            }
        c = c * (1.0f / float(s.lamps.size()));
        // Busca un sitio libre alrededor (suelo debajo y sin nada encima).
        bool found = false;
        for (int t = 0; t < 24 && !found; ++t) {
            float a = float(t) * 2.399f + float(i), r = 7.0f + float(t % 4);
            Vec3 p{c.x + std::cos(a) * r, 0, c.z + std::sin(a) * r};
            p.y = groundHeight(p + Vec3{0, 6, 0});
            if (length(flat(p - heroStart_)) < 5.0f) continue;
            RayHit under;  // sin suelo debajo (barrancos) no se pone
            if (!world_.ground && !segmentVsWorld(p + Vec3{0, 0.3f, 0}, p - Vec3{0, 0.3f, 0}, world_, under)) continue;
            bool blocked = false;
            Vec3 q = p + Vec3{0, 0.6f, 0};
            for (const AABB& b : world_.solids)
                blocked |= q.x > b.min.x - 0.5f && q.x < b.max.x + 0.5f && q.y > b.min.y && q.y < b.max.y && q.z > b.min.z - 0.5f &&
                           q.z < b.max.z + 0.5f;
            if (blocked) continue;
            s.pos = p;
            found = true;
        }
        if (!found) continue;
        switches_.push_back(s);
    }
}

void Game::swapMode() {
    if (!shadowUnlocked_ || swapCooldown_ > 0 || inLight_ || travel_ || diving_) return;
    if (form_ != Form::None) endForm();  // cada forma es de una mitad
    shadowMode_ = !shadowMode_;
    swapCooldown_ = kSwapCooldown;
    modeFlash_ = kModeSwapTime;
    lightLock_ = false;
    inv_.shadowSide = shadowMode_;  // cada mitad lleva su arma
    applyWeapon();
    // Transformación: una espiral del color nuevo sube del suelo a la cabeza, un anillo
    // abajo y otro arriba, y la sombra deja además humo oscuro (la luz, destellos).
    uint32_t col = shadowMode_ ? kShadowCol : rgba(255, 226, 160);
    uint32_t col2 = shadowMode_ ? kShadowDark : rgba(255, 255, 240);
    Vec3 base = hero_.body.pos;
    spawnRing(base + Vec3{0, 0.05f, 0}, 2.6f, 0.5f, col);
    spawnRing(base + Vec3{0, 1.9f, 0}, 1.2f, 0.4f, col2);
    for (int i = 0; i < 48 && particles_.size() < kMaxParticles; ++i) {
        float t = float(i) / 48.0f, a = t * 4.0f * kPi + (shadowMode_ ? kPi : 0);
        Vec3 p = base + Vec3{std::cos(a) * 0.6f, 0.1f + t * 1.9f, std::sin(a) * 0.6f};
        Vec3 v = Vec3{-std::sin(a), 0.4f, std::cos(a)} * 2.5f;
        particles_.push_back({p, v, 0.55f, 0.55f, shadowMode_ ? 0.12f : 0.08f, i % 3 ? col : col2, shadowMode_ ? -1.0f : 0.0f,
                              !shadowMode_});
    }
    spawnSparks(base + Vec3{0, 1, 0}, {0, 1, 0}, 24, col, 4.5f, !shadowMode_);
    shake_ = std::max(shake_, 0.15f);
    sound(Sfx::Echo, hero_.body.pos, 0.8f, shadowMode_ ? 0.55f : 1.4f);
}

// Portales: cruzarlos cambia de luz a sombra o al revés (una vez por cruce).
void Game::updatePortals(float dt) {
    for (Portal& p : portals_) {
        p.spin += dt;
        bool in = length(flat(hero_.body.pos - p.pos)) < 1.1f && std::fabs(hero_.body.pos.y - p.pos.y) < 2.0f;
        if (in && !p.inside && !inLight_ && !travel_ && !diving_ && shadowUnlocked_) {
            swapCooldown_ = 0;
            swapMode();
            shake_ = std::max(shake_, 0.2f);
        }
        p.inside = in;
    }
}

void Game::startDive() {
    diving_ = true;
    diveFrom_ = hero_.body.pos;
    lastDark_ = hero_.body.pos;
    lock_ = -1;
    hero_.setGuard(false);
    spawnRing(hero_.body.pos + Vec3{0, 0.04f, 0}, 1.4f, 0.4f, kShadowCol);
    sound(Sfx::Dodge, hero_.body.pos, 0.8f, 0.5f);
}

// --- Trepar como sombra -------------------------------------------------------------------

namespace {
constexpr float kClimbSpeed = 5.5f;
constexpr float kMinWall = 0.9f;  // lo más bajo ya se sube andando; esto es una pared
}  // namespace

// Empujando contra un bloque alto cuya cara está a oscuras: la sombra se pega a ella.
bool Game::tryWallClimb(const Controls& c) {
    if (length(c.move) < 0.3f) return false;
    Vec3 dir = normalize(flat(c.move)), p = hero_.body.pos;
    Vec3 probe = p + dir * (hero_.body.radius + 0.25f) + Vec3{0, 0.3f, 0};
    for (size_t i = 0; i < world_.solids.size(); ++i) {
        const AABB& b = world_.solids[i];
        if (b.max.y - p.y < kMinWall || b.min.y > p.y + 0.3f) continue;
        if (probe.x < b.min.x || probe.x > b.max.x || probe.z < b.min.z || probe.z > b.max.z) continue;
        // La cara: el lado de la caja por el que está fuera el protagonista.
        float out[4] = {b.min.x - p.x, p.x - b.max.x, b.min.z - p.z, p.z - b.max.z};
        const Vec3 normals[4] = {{-1, 0, 0}, {1, 0, 0}, {0, 0, -1}, {0, 0, 1}};
        int face = 0;
        for (int k = 1; k < 4; ++k)
            if (out[k] > out[face]) face = k;
        if (out[face] < -0.05f) continue;  // dentro: no es una cara
        Vec3 n = normals[face];
        if (dot(dir, -n) < 0.5f) continue;  // tiene que empujar de frente
        Vec3 onFace = p + n * (-out[face]);
        if (shadowAt(onFace + Vec3{0, 1.0f, 0}) < 0.6f) continue;  // pared iluminada: no
        onWall_ = true;
        wallBox_ = int(i);
        wallNormal_ = n;
        wallBase_ = p.y;
        hero_.slide(onFace + n * hero_.body.radius + Vec3{0, 0.2f, 0});
        spawnRing(onFace + Vec3{0, 0.3f, 0}, 1.0f, 0.3f, kShadowCol);
        sound(Sfx::Dodge, p, 0.6f, 0.6f);
        return true;
    }
    return false;
}

// En la pared: empujar hacia ella sube, alejarse baja y los lados mueven a lo largo de la
// cara. La luz tampoco deja pasar aquí.
void Game::updateWallClimb(float dt, const Controls& c) {
    const AABB& b = world_.solids[size_t(wallBox_)];
    Vec3 n = wallNormal_, side{-n.z, 0, n.x};
    Vec3 move = flat(c.move);
    float up = dot(move, -n), along = dot(move, side);
    Vec3 p = hero_.body.pos + Vec3{0, up, 0} * (kClimbSpeed * dt) + side * (along * kClimbSpeed * dt);
    // A lo largo de la cara, sin salirse de ella.
    float margin = 0.25f;
    if (side.x != 0) p.x = clampf(p.x, b.min.x + margin, b.max.x - margin);
    else p.z = clampf(p.z, b.min.z + margin, b.max.z - margin);
    if (std::fabs(along) > 0.1f || std::fabs(up) > 0.1f) hero_.yaw = std::atan2(-n.x, -n.z);
    if (p.y + 0.2f >= b.max.y && up > 0) {  // arriba: pasa al tejado
        float depth = std::min(0.6f, 0.5f * (n.x != 0 ? b.max.x - b.min.x : b.max.z - b.min.z));
        Vec3 top = p - n * (hero_.body.radius + depth);
        top.y = b.max.y + 0.02f;  // justo encima (en el borde exacto la física lo cuenta dentro)
        onWall_ = false;
        hero_.slide(top);
        hero_.body.grounded = true;
        lastDark_ = top;
        spawnRing(top + Vec3{0, 0.04f, 0}, 1.0f, 0.3f, kShadowCol);
        return;
    }
    if (p.y <= wallBase_ && up < 0) {  // abajo: vuelve al suelo
        onWall_ = false;
        Vec3 foot = p + n * 0.15f;
        foot.y = wallBase_;
        hero_.slide(foot);
        hero_.body.grounded = true;
        lastDark_ = foot;
        return;
    }
    p.y = std::max(p.y, wallBase_);
    if (shadowAt(p - n * hero_.body.radius) < 0.45f) p = hero_.body.pos;  // la luz no deja pasar
    hero_.slide(p);
}

// Salir de la sombra: de pie donde está o, con el clic, golpeando desde abajo al enemigo
// más cercano.
void Game::endDive(bool ambush) {
    diving_ = false;
    if (onWall_) {  // se despega de la pared y cae
        onWall_ = false;
        hero_.slide(hero_.body.pos + wallNormal_ * 0.3f);
        hero_.body.vel = wallNormal_ * 3.0f;
    }
    Vec3 at = hero_.body.pos;
    spawnRing(at + Vec3{0, 0.04f, 0}, ambush ? 3.0f : 1.6f, 0.45f, kShadowCol);
    spawnSparks(at + Vec3{0, 0.5f, 0}, {0, 1, 0}, 20, kShadowDark, 4.0f, false);
    sound(Sfx::Dodge, at, 0.8f, 0.7f);
    if (!ambush) return;
    const WeaponStats& w = inv_.weaponStats();
    for (Enemy& e : enemies_) {
        if (!e.targetable()) continue;
        Vec3 to = flat(e.body.pos - at);
        if (length(to) > 3.0f + e.body.radius) continue;
        hitEnemy(e, length(to) > 0.1f ? normalize(to) : hero_.forward(), (w.power + inv_.powerBonus) * 2.2f, 3.0f, 7.0f, true);
    }
    hero_.body.vel.y = 6.0f;  // sale de la sombra de un salto
    shake_ = std::max(shake_, 0.4f);
    sound(Sfx::HitHeavy, at, 0.9f, 0.8f);
}

void Game::updateShadow(float dt, const Controls& c) {
    float dark = shadowAt(hero_.body.pos);
    if (diving_) {
        mp_ -= kDiveCost * dt;
        if (random01() < dt * 40.0f && particles_.size() < 900)
            particles_.push_back({hero_.body.pos + Vec3{randRange(-0.4f, 0.4f), 0.05f, randRange(-0.4f, 0.4f)}, {0, 0.6f, 0},
                                  0.7f, 0.7f, 0.14f, kShadowDark, 0, false});
        if (mp_ <= 0) {
            mp_ = 0;
            endDive(false);
            notify("Sin Poder", rgba(180, 160, 210));
        } else if (c.attack) {
            endDive(true);
        } else if (c.light || c.jump) {
            endDive(false);
        }
        return;
    }
    if (c.light) {
        if (dark > 0.6f && mp_ > 1.0f) startDive();
        else if (lightHint_ <= 0) {
            notify(mp_ <= 1.0f ? "Sin Poder" : "Busca la oscuridad: lejos de las farolas encendidas (o rómpelas)",
                   rgba(180, 160, 210));
            lightHint_ = 2.5f;
        }
    }
}

// Fundido con la sombra, la luz no deja pasar: si el movimiento le lleva a una zona
// iluminada, vuelve al último punto oscuro.
void Game::constrainDive() {
    if (!diving_ || onWall_) return;
    if (shadowAt(hero_.body.pos) < 0.45f) {
        hero_.body.pos.x = lastDark_.x;
        hero_.body.pos.z = lastDark_.z;
    } else {
        lastDark_ = hero_.body.pos;
    }
}


void Game::updateBlackHoles(float dt) {
    for (BlackHole& h : holes_) {
        h.age += dt;
        for (Enemy& e : enemies_) {
            if (!e.targetable()) continue;
            Vec3 to = h.pos - e.center();
            float d = length(to);
            if (d > (h.bomb ? kBombPull : kHolePull)) continue;
            float pull = (e.boss ? 0.4f : 1.0f) * std::min(h.bomb ? 4.0f : 6.0f, d * 2.5f);
            e.knock = normalize(flat(to)) * pull;
            if (d < 2.2f && !h.bomb) {  // se van deshaciendo dentro
                e.health -= 16.0f * dt * (e.def().shadow ? 0.8f : 1.5f);
                if (!e.boss) {
                    e.state = AiState::Stagger;
                    e.stateTime = 0;
                }
                if (e.health <= 0) killEnemy(e);
            }
        }
        for (Projectile& p : projectiles_) {
            if (p.kind != ProjectileKind::EnemyOrb || p.dead) continue;
            Vec3 to = h.pos - p.pos;
            if (length(to) < 0.8f) p.dead = true;
            else if (length(to) < 6.0f) p.vel = normalize(p.vel + normalize(to) * 30.0f * dt) * length(p.vel);
        }
        if (random01() < dt * 60.0f && particles_.size() < 900) {  // lo que cae hacia dentro
            float a = random01() * 2.0f * kPi, r = randRange(1.5f, 3.0f);
            Vec3 from = h.pos + Vec3{std::cos(a) * r, randRange(-0.8f, 0.8f), std::sin(a) * r};
            particles_.push_back({from, (h.pos - from) * 2.2f, 0.45f, 0.45f, 0.07f, random01() < 0.5f ? kShadowCol : rgba(255, 230, 200),
                                  0, true});
        }
        if (h.age >= h.life) {  // implosión (la mina, más fuerte y más ancha)
            const WeaponStats& w = inv_.weaponStats();
            float radius = h.bomb ? 4.5f : 3.5f;
            float damage = (w.power + inv_.powerBonus + levelPower()) * (h.bomb ? 3.5f : 3.0f) + w.magic;
            for (Enemy& e : enemies_) {
                if (!e.targetable()) continue;
                Vec3 to = flat(e.center() - h.pos);
                if (length(to) > radius + e.body.radius) continue;
                hitEnemy(e, length(to) > 0.1f ? normalize(to) : Vec3{1, 0, 0}, damage, 9.0f, h.bomb ? 6.0f : 4.0f, true);
            }
            for (Crate& cr : crates_)
                if (!cr.broken && length(cr.box.center() - h.pos) < radius) breakCrate(cr);
            Vec3 floor{h.pos.x, groundHeight(h.pos) + 0.05f, h.pos.z};
            spawnRing(floor, radius + 0.5f, 0.5f, kShadowCol);
            if (h.bomb) spawnRing(floor, radius * 0.6f, 0.35f, kShadowDark);
            spawnSparks(h.pos, {0, 1, 0}, 30, kShadowCol, 7.0f);
            spawnSparks(h.pos, {0, 1, 0}, 16, kShadowDark, 5.0f, false);
            shake_ = std::max(shake_, h.bomb ? 0.5f : 0.6f);
            hitStop_ = 0.1f;
            sound(Sfx::FireBoom, h.pos, 1.1f, 0.6f);
        }
    }
    holes_.erase(std::remove_if(holes_.begin(), holes_.end(), [](const BlackHole& h) { return h.age >= h.life; }), holes_.end());
}
