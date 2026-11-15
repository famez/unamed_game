// La sombra, la otra mitad del protagonista. Se cambia de luz a sombra (y al revés)
// cruzando un portal del escenario.
//   - La oscuridad es donde no llega la luz: lejos de las farolas encendidas (cada una alumbra
//     según la energía que le queda), de los nodos y de los faroles.
//   - Fundirse (Q en la oscuridad): el protagonista se hace sombra en el suelo y se mueve
//     por toda la zona oscura, rápido e invisible; la luz no le deja pasar. Gasta Sombra, y la
//     sombra que gasta se convierte en luz: las farolas apagadas que roza se van encendiendo.
//     Sale con Q o Espacio, o con el clic en emboscada sobre el enemigo más cercano.
//   - Agujero negro (V, 60 de Sombra): atrae a los enemigos y a sus proyectiles, los va
//     deshaciendo y acaba en una implosión.
//   - Golpes cruzados: la sombra hiere más a los de luz (y les arranca Sombra); la luz, a los
//     de sombra.

#include "game/game.h"

#include <algorithm>

namespace {
constexpr float kSwapCooldown = 0.8f;
constexpr float kDiveCost = 6.0f;          // Sombra por segundo fundido en la sombra
constexpr float kDiveSpeed = 1.7f;         // multiplicador de la carrera
constexpr float kRelightRate = 0.35f;      // energía por segundo que recuperan las farolas cercanas
constexpr float kRelightRange = 5.0f;
constexpr float kShadowAbsorb = 30.0f;     // Sombra por segundo en plena oscuridad
constexpr float kHoleCost = 60.0f, kHoleLife = 2.6f, kHolePull = 7.0f;
const uint32_t kShadowCol = rgba(150, 80, 255), kShadowDark = rgba(30, 16, 48);
}  // namespace

// Radio que alumbra una farola según su energía.
float lampRadius(const Lamp& l) { return 2.5f + 4.0f * l.energy; }

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

float Game::focusOpen(Vec3 p) const {
    float open = 1;
    for (const ShadowFocus& f : foci_) {
        if (std::fabs(p.y - f.pos.y) > 3.0f) continue;
        open = std::min(open, smoothstep01((length(flat(p - f.pos)) - f.radius * 0.6f) / (f.radius * 0.4f)));
    }
    return open;
}

float Game::shadowAt(Vec3 p) const { return 1.0f - std::max(lightAtRaw(p), lampLightAt(p)) * focusOpen(p); }

// Al absorber luz el foco de sombra de ese sitio crece (o nace uno). Devuelve false si ya
// tiene el tamaño máximo: ahí no queda luz.
bool Game::growFocus(Vec3 at, float dt) {
    ShadowFocus* f = nullptr;
    for (ShadowFocus& g : foci_)
        if (length(flat(g.pos - at)) < std::max(1.5f, g.radius * 0.5f) && std::fabs(g.pos.y - at.y) < 1.5f) f = &g;
    if (!f) {
        if (foci_.size() >= 12) foci_.erase(foci_.begin());
        foci_.push_back({at, 0.6f, 0});
        f = &foci_.back();
        spawnRing(at + Vec3{0, 0.04f, 0}, 1.0f, 0.4f, kShadowCol);
    }
    if (f->radius >= kFocusMax) return false;
    f->radius = std::min(kFocusMax, f->radius + 1.4f * dt);
    f->age = 0;
    if (random01() < dt * 30.0f && particles_.size() < 900) {  // humo oscuro que brota del borde
        float a = random01() * 2.0f * kPi;
        Vec3 p = f->pos + Vec3{std::cos(a) * f->radius * 0.8f, 0.05f, std::sin(a) * f->radius * 0.8f};
        particles_.push_back({p, {0, 0.7f, 0}, 0.9f, 0.9f, 0.18f, kShadowDark, 0, false});
    }
    return true;
}

// Los focos aguantan un buen rato y luego se cierran despacio.
void Game::updateFoci(float dt) {
    for (ShadowFocus& f : foci_) {
        f.age += dt;
        if (f.age > 60.0f) f.radius -= 0.05f * dt;
    }
    foci_.erase(std::remove_if(foci_.begin(), foci_.end(), [](const ShadowFocus& f) { return f.radius < 0.3f; }), foci_.end());
}

// Absorber sombra (modo sombra, Q mantenida en la oscuridad): da Sombra; los focos de
// alrededor se deshacen y las farolas apagadas cercanas se van encendiendo.
void Game::absorbShadow(float dt) {
    Vec3 chest = hero_.body.pos + Vec3{0, 1.0f, 0};
    float dark = shadowAt(hero_.body.pos);
    if (dark < 0.5f) {
        if (absorbHint_ <= 0) notify("Aquí no hay sombra que absorber", rgba(180, 160, 210));
        absorbHint_ = 3.0f;
        return;
    }
    shadow_ = std::min(maxShadow_, shadow_ + kShadowAbsorb * dark * dt);
    for (ShadowFocus& f : foci_)
        if (length(flat(f.pos - hero_.body.pos)) < f.radius + 2.0f) f.radius -= 1.0f * dt;
    for (size_t i = 0; i < lamps_.size(); ++i) {
        Lamp& l = lamps_[i];
        if (!l.bulb || l.on || length(flat(l.pos - hero_.body.pos)) > kRelightRange) continue;
        l.energy += kRelightRate * 0.6f * dt;
        if (l.energy >= 1.0f) relightLamp(int(i));
    }
    if (random01() < dt * 45.0f && particles_.size() < 900) {  // la sombra sube del suelo al pecho
        float a = random01() * 2.0f * kPi, r = randRange(1.0f, 3.0f);
        Vec3 from = hero_.body.pos + Vec3{std::cos(a) * r, 0.05f, std::sin(a) * r};
        particles_.push_back({from, (chest - from) * 1.6f, 0.6f, 0.6f, 0.08f, random01() < 0.6f ? kShadowDark : kShadowCol, 0, true});
    }
}

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
    l.on = true;
    l.energy = 1.0f;
    Vec3 base{l.pos.x, groundHeight(l.pos + Vec3{0.6f, 0, 0}), l.pos.z};
    spawnSparks(l.pos, {0, -1, 0}, 24, l.color, 3.0f);
    spawnRing(base + Vec3{0, 0.05f, 0}, 4.0f, 0.6f, rgba(255, 226, 160));
    sound(Sfx::ItemGet, l.pos, 0.7f, 1.2f);
}

void Game::swapMode() {
    if (!shadowUnlocked_ || swapCooldown_ > 0 || inLight_ || travel_ || diving_) return;
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

// Salir de la sombra: de pie donde está o, con el clic, golpeando desde abajo al enemigo
// más cercano.
void Game::endDive(bool ambush) {
    diving_ = false;
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
        shadow_ -= kDiveCost * dt;
        // La sombra que se gasta se hace luz: las farolas apagadas cercanas se van encendiendo.
        for (size_t i = 0; i < lamps_.size(); ++i) {
            Lamp& l = lamps_[i];
            if (!l.bulb || l.on || length(flat(l.pos - hero_.body.pos)) > kRelightRange) continue;
            l.energy += kRelightRate * dt;
            if (random01() < dt * 10.0f && particles_.size() < 900)
                particles_.push_back({hero_.body.pos + Vec3{0, 0.1f, 0}, (l.pos - hero_.body.pos) * 1.2f, 0.8f, 0.8f, 0.05f,
                                      rgba(255, 226, 170), 0, true});
            if (l.energy >= 1.0f) relightLamp(int(i));
        }
        if (random01() < dt * 40.0f && particles_.size() < 900)
            particles_.push_back({hero_.body.pos + Vec3{randRange(-0.4f, 0.4f), 0.05f, randRange(-0.4f, 0.4f)}, {0, 0.6f, 0},
                                  0.7f, 0.7f, 0.14f, kShadowDark, 0, false});
        if (shadow_ <= 0) {
            shadow_ = 0;
            endDive(false);
            notify("Sin Sombra", rgba(180, 160, 210));
        } else if (c.attack) {
            endDive(true);
        } else if (c.light || c.jump) {
            endDive(false);
        }
        return;
    }
    hero_.setAbsorb(c.absorb);
    if (hero_.absorbing()) absorbShadow(dt);
    if (c.light && !hero_.absorbing()) {
        if (dark > 0.6f && shadow_ > 1.0f) startDive();
        else if (lightHint_ <= 0) {
            notify(shadow_ <= 1.0f ? "Sin Sombra" : "Busca la oscuridad: lejos de las farolas encendidas", rgba(180, 160, 210));
            lightHint_ = 2.5f;
        }
    }
    if (c.nova) {
        if (shadow_ < kHoleCost) notify("El agujero negro necesita 60 de Sombra", rgba(180, 160, 210));
        else spawnBlackHole();
    }
}

// Fundido con la sombra, la luz no deja pasar: si el movimiento le lleva a una zona
// iluminada, vuelve al último punto oscuro.
void Game::constrainDive() {
    if (!diving_) return;
    if (shadowAt(hero_.body.pos) < 0.45f) {
        hero_.body.pos.x = lastDark_.x;
        hero_.body.pos.z = lastDark_.z;
    } else {
        lastDark_ = hero_.body.pos;
    }
}

void Game::spawnBlackHole() {
    shadow_ -= kHoleCost;
    Vec3 at = hero_.body.pos + hero_.forward() * 3.5f + Vec3{0, 1.2f, 0};
    Vec3 target;
    if (lockTarget(target) && length(flat(target - hero_.body.pos)) < 12.0f) at = target + Vec3{0, 0.4f, 0};
    holes_.push_back({at, kHoleLife, 0});
    hero_.cast(hero_.yaw);
    shake_ = std::max(shake_, 0.3f);
    sound(Sfx::Echo, at, 1.0f, 0.45f);
    sound(Sfx::WaveStart, at, 0.7f, 1.4f);
}

void Game::updateBlackHoles(float dt) {
    for (BlackHole& h : holes_) {
        h.age += dt;
        for (Enemy& e : enemies_) {
            if (!e.targetable()) continue;
            Vec3 to = h.pos - e.center();
            float d = length(to);
            if (d > kHolePull) continue;
            float pull = (e.boss ? 0.4f : 1.0f) * std::min(6.0f, d * 2.5f);
            e.knock = normalize(flat(to)) * pull;
            if (d < 2.2f) {  // se van deshaciendo dentro
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
        if (h.age >= h.life) {  // implosión
            const WeaponStats& w = inv_.weaponStats();
            for (Enemy& e : enemies_) {
                if (!e.targetable()) continue;
                Vec3 to = flat(e.center() - h.pos);
                if (length(to) > 3.5f + e.body.radius) continue;
                hitEnemy(e, length(to) > 0.1f ? normalize(to) : Vec3{1, 0, 0}, (w.power + inv_.powerBonus) * 3.0f, 9.0f, 4.0f, true);
            }
            spawnRing(Vec3{h.pos.x, groundHeight(h.pos) + 0.05f, h.pos.z}, 4.0f, 0.5f, kShadowCol);
            spawnSparks(h.pos, {0, 1, 0}, 30, kShadowCol, 7.0f);
            shake_ = std::max(shake_, 0.6f);
            hitStop_ = 0.1f;
            sound(Sfx::FireBoom, h.pos, 1.1f, 0.6f);
        }
    }
    holes_.erase(std::remove_if(holes_.begin(), holes_.end(), [](const BlackHole& h) { return h.age >= h.life; }), holes_.end());
}
