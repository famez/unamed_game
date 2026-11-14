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
constexpr float kTravelRange = 35.0f, kTravelCost = 10.0f, kLightRecharge = 3.0f;
}  // namespace

// Fuentes de luz a las que se puede viajar: farolas (y el prisma de la fuente), nodos y
// faroles encendidos, emisores de los rompecabezas.
void Game::gatherLightPoints() {
    lightPoints_.clear();
    for (Vec3 l : lamps_) lightPoints_.push_back({l, {}});
    for (const ColorNode& n : nodes_)
        if (n.lit) lightPoints_.push_back({n.pos + Vec3{0, 1.75f, 0}, {}});
    for (const Lantern& ln : lanterns_)
        if (ln.lit) lightPoints_.push_back({ln.pos + Vec3{0.42f, 1.95f, 0}, {}});
    for (const LightEmitter& em : emitters_) lightPoints_.push_back({em.pos, {}});
    // Se llega junto a la luz, sobre lo que haya debajo (por el lado del Prismero).
    for (LightPoint& p : lightPoints_) {
        Vec3 toHero = normalize(flat(hero_.body.pos - p.pos));
        Vec3 probe = p.pos + toHero * 0.3f + Vec3{0, 0.4f, 0};  // desde encima de la luz hacia abajo
        p.arrive = {probe.x, groundHeight(probe), probe.z};
        if (!world_.ground && p.arrive.y <= 0.0f) {  // sobre el vacío: se queda a la altura de la luz
            RayHit h;
            if (!segmentVsWorld(probe, probe - Vec3{0, 30, 0}, world_, h)) p.arrive.y = p.pos.y - 0.5f;
        }
    }
}

// La fuente que señala la cámara: la más centrada (poco ángulo o poca distancia al rayo),
// a menos de 35 m y con vista directa desde el Prismero.
void Game::updateLightTarget(const Controls& c) {
    lightTarget_ = -1;
    if (travel_ || hero_.busy() || hero_.downTimer >= 0) return;
    gatherLightPoints();
    Vec3 chest = hero_.body.pos + Vec3{0, 1.0f, 0};
    Vec3 dir = normalize(c.aimDir);
    float best = 1e9f;
    for (size_t i = 0; i < lightPoints_.size(); ++i) {
        const LightPoint& p = lightPoints_[i];
        float dh = length(p.pos - chest);
        if (dh > kTravelRange || dh < 2.5f || length(flat(p.arrive - hero_.body.pos)) < 1.5f) continue;
        Vec3 v = p.pos - c.aimOrigin;
        float along = dot(v, dir);
        if (along <= 0) continue;
        float off = length(v - dir * along);          // distancia al rayo de la cámara
        if (off > 0.6f + along * 0.06f) continue;    // ~3,5° de margen más un poco
        // Hace falta ver la luz o el sitio al que se llega.
        RayHit h;
        Vec3 stop = p.pos - normalize(p.pos - chest) * 0.7f;
        if (segmentVsWorld(chest, stop, world_, h) && segmentVsWorld(chest, p.arrive + Vec3{0, 1.2f, 0}, world_, h)) continue;
        float score = off / along;
        if (score < best) best = score, lightTarget_ = int(i);
    }
}

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

    // En la luz, la Luz se recarga despacio sola.
    if (here > 0.3f) mp_ = std::min(maxMp_, mp_ + kLightRecharge * here * dt);

    // Viajar por la luz: si se apunta a una fuente, Q lleva hasta ella.
    if (c.beam && lightTarget_ >= 0) {
        float cost = here > 0.3f ? 0.0f : kTravelCost;
        const LightPoint& p = lightPoints_[size_t(lightTarget_)];
        if (mp_ < cost) {
            notify("No te queda Luz para viajar", rgba(200, 200, 215));
        } else {
            float dist = length(p.arrive - hero_.body.pos);
            travelDur_ = 0.25f + dist / 45.0f;
            if (hero_.startBeam(p.arrive - hero_.body.pos, travelDur_ + 0.1f)) {
                mp_ -= cost;
                travel_ = true;
                travelTime_ = 0;
                travelFrom_ = hero_.body.pos;
                travelTo_ = p.arrive;
                lock_ = -1;
                sound(Sfx::Echo, hero_.body.pos, 0.7f, 1.6f);
                spawnSparks(chest, {0, 1, 0}, 20, rgba(255, 245, 210), 4.0f);
            }
        }
        return;
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

// Durante el viaje el Prismero es luz que vuela en arco hasta la fuente (atraviesa lo que
// haya en medio); al llegar recupera su forma junto a ella.
void Game::updateTravel(float dt) {
    if (!travel_) return;
    travelTime_ += dt;
    float u = clampf(travelTime_ / travelDur_, 0, 1);
    float e = smoothstep01(u);
    float arc = std::min(3.0f, length(travelTo_ - travelFrom_) * 0.15f);
    hero_.body.pos = travelFrom_ + (travelTo_ - travelFrom_) * e + Vec3{0, std::sin(u * kPi) * arc, 0};
    hero_.body.vel = {};
    if (random01() < dt * 60.0f && particles_.size() < 900)
        particles_.push_back({hero_.body.pos + Vec3{0, 1, 0}, {0, 0.2f, 0}, 0.4f, 0.4f, 0.08f, rgba(255, 240, 200), 0, true});
    if (u >= 1.0f) {
        travel_ = false;
        hero_.body.pos = travelTo_ + Vec3{0, 0.05f, 0};
        hero_.stopBeam();
        spawnRing(travelTo_ + Vec3{0, 0.05f, 0}, 1.5f, 0.5f, rgba(255, 240, 200));
        spawnSparks(travelTo_ + Vec3{0, 1, 0}, {0, 1, 0}, 24, rgba(255, 245, 210), 4.0f);
        sound(Sfx::PerfectGuard, travelTo_, 0.5f, 1.3f);
    }
}
