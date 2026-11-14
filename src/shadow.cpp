// La sombra, la otra mitad del protagonista (Z para cambiar cuando ya la ha despertado):
//   - Fundirse: dentro de un charco de oscuridad corre más, los enemigos no le ven salvo
//     muy de cerca y recupera Sombra poco a poco.
//   - Beber la luz (mantener Q junto a una farola encendida): la farola se apaga y deja un
//     charco de oscuridad a su alrededor. En modo luz se puede volver a encender (F).
//   - Deslizarse (pulsar Q dentro de un charco): viaja por el suelo hasta otro charco que
//     señale la cámara.
//   - Agujero negro (V, 60 de Sombra): atrae a los enemigos y a sus proyectiles, los va
//     deshaciendo y acaba en una implosión.
//   - Golpes cruzados: la sombra hiere más a los de luz (y les arranca Sombra); la luz, a los
//     de sombra.

#include "game.h"

#include <algorithm>

namespace {
constexpr float kSwapCooldown = 0.8f;
constexpr float kPoolRegen = 4.0f;        // Sombra por segundo dentro de un charco
constexpr float kDrinkTime = 0.9f;        // segundos bebiendo para apagar una farola
constexpr float kDrinkGain = 30.0f;
constexpr float kRelightCost = 15.0f;
constexpr float kHoleCost = 60.0f, kHoleLife = 2.6f, kHolePull = 7.0f;
const uint32_t kShadowCol = rgba(150, 80, 255), kShadowDark = rgba(30, 16, 48);
}  // namespace

float Game::shadowAt(Vec3 p) const {
    float s = 0;
    for (const ShadowPool& pl : pools_) {
        if (std::fabs(p.y - pl.pos.y) > 3.0f) continue;
        float r = pl.radius * smoothstep01(pl.age / 0.8f);
        if (r > 0) s = std::max(s, 1.0f - smoothstep01((length(flat(p - pl.pos)) - r * 0.6f) / (r * 0.4f)));
    }
    return s;
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

void Game::addPool(Vec3 pos, float radius) {
    // El suelo se mide algo apartado del centro (ahí suele estar el poste de la farola).
    pools_.push_back({Vec3{pos.x, groundHeight(pos + Vec3{0.6f, 0.5f, 0}), pos.z}, radius, 0});
}

void Game::relightLamp(int i) {
    Lamp& l = lamps_[size_t(i)];
    l.on = true;
    Vec3 base{l.pos.x, groundHeight(l.pos + Vec3{0.6f, 0, 0}), l.pos.z};
    pools_.erase(std::remove_if(pools_.begin(), pools_.end(),
                                [&](const ShadowPool& p) { return length(flat(p.pos - base)) < 1.5f; }),
                 pools_.end());
    spawnSparks(l.pos, {0, -1, 0}, 24, l.color, 3.0f);
    spawnRing(base + Vec3{0, 0.05f, 0}, 4.0f, 0.6f, rgba(255, 226, 160));
    sound(Sfx::ItemGet, l.pos, 0.7f, 1.2f);
}

void Game::swapMode() {
    if (!shadowUnlocked_ || swapCooldown_ > 0 || inLight_ || travel_) return;
    shadowMode_ = !shadowMode_;
    swapCooldown_ = kSwapCooldown;
    modeFlash_ = 0.5f;
    drink_ = 0;
    lightLock_ = false;
    uint32_t col = shadowMode_ ? kShadowCol : rgba(255, 226, 160);
    spawnRing(hero_.body.pos + Vec3{0, 0.05f, 0}, 2.2f, 0.45f, col);
    spawnSparks(hero_.body.pos + Vec3{0, 1, 0}, {0, 1, 0}, 18, col, 3.0f);
    sound(Sfx::Echo, hero_.body.pos, 0.6f, shadowMode_ ? 0.6f : 1.3f);
}

void Game::updateShadow(float dt, const Controls& c) {
    Vec3 chest = hero_.body.pos + Vec3{0, 1.0f, 0};
    float dark = shadowAt(hero_.body.pos);
    if (dark > 0.5f) shadow_ = std::min(maxShadow_, shadow_ + kPoolRegen * dark * dt);
    // Fundido con la sombra: deja un rastro de humo oscuro al moverse.
    if (dark > 0.5f && length(flat(hero_.body.vel)) > 1.0f && random01() < dt * 30.0f && particles_.size() < 900)
        particles_.push_back({hero_.body.pos + Vec3{randRange(-0.3f, 0.3f), randRange(0.1f, 1.4f), randRange(-0.3f, 0.3f)},
                              {0, 0.4f, 0}, 0.6f, 0.6f, 0.12f, kShadowDark, 0, false});

    // Deslizarse hasta otro charco.
    if (c.light && lightTarget_ >= 0) {
        startTravel(lightPoints_[size_t(lightTarget_)].arrive, 3);
        sound(Sfx::Dodge, hero_.body.pos, 0.8f, 0.6f);
        return;
    }
    if (c.light && lightHint_ <= 0) {
        notify(dark > 0.5f ? "Apunta a otra sombra para deslizarte" : "Solo puedes deslizarte desde la sombra",
               rgba(180, 160, 210));
        lightHint_ = 2.5f;
    }

    // Beber la luz de una farola: la apaga y deja oscuridad.
    int lamp = c.absorb ? nearLamp(4.0f, true) : -1;
    hero_.setAbsorb(lamp >= 0);
    if (lamp >= 0) {
        if (lamp != drinkLamp_) drink_ = 0;
        drinkLamp_ = lamp;
        drink_ += dt / kDrinkTime;
        Lamp& l = lamps_[size_t(lamp)];
        if (random01() < dt * 50.0f && particles_.size() < 900) {
            Vec3 from = l.pos + Vec3{randRange(-0.2f, 0.2f), randRange(-0.2f, 0.2f), randRange(-0.2f, 0.2f)};
            particles_.push_back({from, (chest - from) * 2.0f, 0.5f, 0.5f, 0.06f, l.color, 0, true});
        }
        if (drink_ >= 1.0f) {
            l.on = false;
            drink_ = 0;
            addPool(Vec3{l.pos.x, l.pos.y - 3.0f, l.pos.z}, 5.5f);
            shadow_ = std::min(maxShadow_, shadow_ + kDrinkGain);
            spawnRing(pools_.back().pos + Vec3{0, 0.05f, 0}, 5.0f, 0.7f, kShadowCol);
            sound(Sfx::Crate, l.pos, 0.8f, 0.6f);
            sound(Sfx::Echo, l.pos, 0.5f, 0.5f);
        }
    } else {
        drink_ = 0;
        drinkLamp_ = -1;
        if (c.absorb && absorbHint_ <= 0) {
            notify("Acércate a una farola encendida para beber su luz", rgba(180, 160, 210));
            absorbHint_ = 3.0f;
        }
    }

    if (c.nova) {
        if (shadow_ < kHoleCost) notify("El agujero negro necesita 60 de Sombra", rgba(180, 160, 210));
        else spawnBlackHole();
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
