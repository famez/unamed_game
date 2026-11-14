// La Luz, la mecánica propia del Prismero:
//   - Absorber (mantener Q): junto a nodos encendidos o faroles se carga Luz; sobre un Gris
//     aturdido se le arranca el color robado (le hace daño y da Luz).
//   - Hacerse luz (pulsar Q): un haz en línea recta que atraviesa a los Grises y cruza
//     vacíos. Cuesta Luz, salvo dentro de una zona con color, donde además llega más lejos.
//   - Supernova (V con la Luz llena): explosión de área que devuelve croma.

#include "game.h"

#include <algorithm>

namespace {
constexpr float kAbsorbRate = 40.0f;     // Luz por segundo en plena luz
constexpr float kDrainRate = 14.0f;      // Luz por segundo arrancada a cada Gris aturdido
constexpr float kDrainDamage = 12.0f;    // daño por segundo al Gris
constexpr float kBeamCost = 20.0f;
constexpr float kBeamTime = 0.31f, kBeamTimeLit = 0.47f;  // ~10 m y ~15 m
constexpr float kNovaRadius = 7.5f;
}  // namespace

float Game::lightAt(Vec3 p) const {
    float l = 0;
    for (const ColorNode& n : nodes_) {
        if (!n.lit) continue;
        float r = n.radius * smoothstep01(n.litTime / 3.0f);
        if (r > 0) l = std::max(l, 1.0f - smoothstep01((length(p - n.pos) - r * 0.55f) / (r * 0.45f)));
    }
    for (const Lantern& ln : lanterns_)
        if (ln.lit) l = std::max(l, 1.0f - smoothstep01((length(p - ln.pos) - 3.0f) / 3.0f));
    return l;
}

void Game::updateLight(float dt, const Controls& c) {
    Vec3 chest = hero_.body.pos + Vec3{0, 1.0f, 0};
    float here = lightAt(hero_.body.pos);
    absorbHint_ = std::max(0.0f, absorbHint_ - dt);
    novaFlash_ = std::max(0.0f, novaFlash_ - dt);

    // Absorber.
    hero_.setAbsorb(c.absorb);
    if (hero_.absorbing()) {
        float gain = kAbsorbRate * here * dt;
        Vec3 source;
        float best = 0;
        for (const ColorNode& n : nodes_)  // de dónde vienen las motas de luz
            if (n.lit && lightAt(n.pos) > best) {
                best = 1;
                source = n.pos + Vec3{0, 1.8f, 0};
            }
        for (const Lantern& ln : lanterns_)
            if (ln.lit && length(ln.pos - hero_.body.pos) < 6.0f) source = ln.pos + Vec3{0.42f, 1.95f, 0};
        bool drained = false;
        for (Enemy& e : enemies_) {
            if (!e.targetable() || e.state != AiState::Stagger || length(flat(e.body.pos - hero_.body.pos)) > 4.0f) continue;
            // Arrancar el color robado: mientras se absorbe sigue aturdido.
            e.stateTime = std::min(e.stateTime, 0.2f);
            e.health -= kDrainDamage * dt;
            gain += kDrainRate * dt;
            drained = true;
            if (random01() < dt * 30.0f && particles_.size() < 900) {
                Vec3 from = e.center();
                particles_.push_back({from, (chest - from) * 2.2f, 0.45f, 0.45f, 0.07f, rgba(255, 120, 200), 0, true});
            }
            if (e.health <= 0) killEnemy(e);
        }
        if (here > 0.05f && random01() < dt * 40.0f * here && particles_.size() < 900) {
            Vec3 from = source + Vec3{randRange(-0.5f, 0.5f), randRange(-0.3f, 0.3f), randRange(-0.5f, 0.5f)};
            particles_.push_back({from, (chest - from) * 1.8f, 0.55f, 0.55f, 0.06f, rgba(255, 240, 190), 0, true});
        }
        if (gain <= 0 && !drained && absorbHint_ <= 0) {
            notify("Aquí no hay luz que absorber", rgba(200, 200, 215));
            absorbHint_ = 3.0f;
        }
        mp_ = std::min(maxMp_, mp_ + gain);
    }

    // Hacerse luz.
    if (c.beam) {
        bool free = here > 0.3f;
        float cost = free ? 0.0f : kBeamCost;
        Vec3 dir = length(c.move) > 0.1f ? c.move : c.camForward;
        if (mp_ < cost) {
            notify("No te queda Luz para el haz", rgba(200, 200, 215));
        } else if (hero_.startBeam(dir, free ? kBeamTimeLit : kBeamTime)) {
            mp_ -= cost;
            beamHits_.clear();
            sound(Sfx::Dodge, hero_.body.pos, 1.0f, 1.6f);
            spawnSparks(chest, -dir, 16, rgba(255, 245, 210), 4.0f);
        }
    }
    if (hero_.beaming()) {
        // Atraviesa a los Grises: cada uno recibe un golpe una sola vez por haz.
        for (size_t i = 0; i < enemies_.size(); ++i) {
            Enemy& e = enemies_[i];
            if (!e.targetable() || std::find(beamHits_.begin(), beamHits_.end(), int(i)) != beamHits_.end()) continue;
            if (length(e.center() - chest) > 1.3f + e.body.radius) continue;
            beamHits_.push_back(int(i));
            Vec3 side{-hero_.beamDir().z, 0, hero_.beamDir().x};
            if (dot(side, e.body.pos - hero_.body.pos) < 0) side = -side;
            hitEnemy(e, normalize(side + hero_.beamDir() * 0.5f), (inv_.weaponStats().power + inv_.powerBonus) * 1.3f, 5.0f,
                     1.5f, true);
            sound(Sfx::Hit, e.center(), 0.9f, 1.3f);
        }
        if (particles_.size() < 900)
            for (int k = 0; k < 3; ++k)
                particles_.push_back({chest + Vec3{randRange(-0.2f, 0.2f), randRange(-0.4f, 0.4f), randRange(-0.2f, 0.2f)},
                                      {0, 0.3f, 0}, 0.35f, 0.35f, 0.08f, k ? rgba(255, 245, 210) : rgba(120, 230, 255), 0, k == 0});
    }

    // Supernova.
    if (c.nova) {
        if (mp_ < maxMp_) {
            notify("La Supernova necesita la Luz llena", rgba(200, 200, 215));
        } else if (hero_.cast(hero_.yaw)) {
            mp_ = 0;
            pendingNova_ = true;
            uiSound(Sfx::Echo, 1.0f, 0.8f);
        }
    }
}

void Game::supernova() {
    Vec3 c = hero_.body.pos + Vec3{0, 1.0f, 0};
    const WeaponStats& w = inv_.weaponStats();
    float damage = (w.power + inv_.powerBonus) * 4.0f + w.magic * 2.0f;
    for (Enemy& e : enemies_) {
        if (!e.targetable()) continue;
        Vec3 to = e.center() - c;
        float d = length(to);
        if (d > kNovaRadius + e.body.radius) continue;
        Vec3 dir = normalize(flat(to));
        if (length(dir) < 0.5f) dir = hero_.forward();
        hitEnemy(e, dir, damage * (1.0f - 0.4f * clampf(d / kNovaRadius, 0, 1)), 11.0f, 7.0f, true);
    }
    for (Crate& cr : crates_)
        if (!cr.broken && length(cr.box.center() - c) < kNovaRadius) breakCrate(cr);
    // Estalla en todos los colores: anillos, chispas y salpicaduras alrededor.
    const uint32_t rainbow[] = {rgba(255, 70, 120), rgba(255, 190, 60), rgba(120, 255, 140), rgba(40, 220, 255),
                                rgba(170, 110, 255), rgba(255, 90, 200)};
    for (int i = 0; i < 6; ++i) {
        spawnRing(hero_.body.pos + Vec3{0, 0.05f + 0.02f * float(i), 0}, kNovaRadius * (0.5f + 0.1f * float(i)), 0.6f + 0.1f * float(i),
                  rainbow[i]);
        spawnSparks(c, {0, 1, 0}, 14, rainbow[i], 9.0f);
        float a = float(i) * kPi / 3.0f + random01();
        Vec3 sp = hero_.body.pos + Vec3{std::cos(a), 0, std::sin(a)} * randRange(2.0f, 5.0f);
        splashes_.push_back({Vec3{sp.x, groundHeight(sp + Vec3{0, 1, 0}), sp.z}, randRange(1.2f, 1.8f), rainbow[i], 0});
    }
    while (splashes_.size() > size_t(Renderer::kMaxPaints)) splashes_.erase(splashes_.begin());
    croma_ = std::min(1.0f, croma_ + 0.08f);
    shake_ = std::max(shake_, 0.7f);
    hitStop_ = 0.12f;
    novaFlash_ = 0.6f;
    sound(Sfx::FireBoom, c, 1.2f, 0.7f);
    uiSound(Sfx::PerfectGuard, 1.0f, 0.8f);
}
