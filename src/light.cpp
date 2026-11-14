// La Luz, la mecánica propia del Prismero:
//   - Absorber (mantener Q): junto a nodos encendidos o faroles se carga Luz; sobre un Gris
//     aturdido se le arranca el color robado (le hace daño y da Luz).
//   - Entrar en la luz (pulsar Q junto a una farola u otra fuente): el Prismero se queda
//     dentro de ella. Desde dentro salta a cualquier otra fuente a la vista (Q; R elige
//     cuál), sale cuando quiere (Espacio) o cae sobre el Gris más cercano (clic: emboscada).
//     Dentro no le ven ni le hieren, recupera Luz y la farola alumbra más (los Grises de
//     debajo se debilitan).
//   - Supernova (V con la Luz llena): explosión de área que devuelve croma.

#include "game.h"

#include <algorithm>

namespace {
constexpr float kAbsorbRate = 40.0f;     // Luz por segundo en plena luz
constexpr float kDrainRate = 14.0f;      // Luz por segundo arrancada a cada Gris aturdido
constexpr float kDrainDamage = 12.0f;    // daño por segundo al Gris
constexpr float kNovaRadius = 7.5f;
constexpr float kLightRecharge = 3.0f;          // Luz por segundo en una zona con luz
constexpr float kEnterRange = 4.0f;             // distancia horizontal para entrar en una fuente
constexpr float kJumpRange = 30.0f;             // de fuente a fuente
constexpr float kInsideRecharge = 12.0f;        // Luz por segundo dentro de una fuente
constexpr float kAmbushRange = 9.0f;            // emboscada: Grises a esta distancia de la fuente
constexpr float kHostGlow = 5.0f;               // radio que alumbra la fuente ocupada
constexpr float kGlideRange = 25.0f;            // de sombra en sombra
}  // namespace

// Fuentes de luz a las que se puede viajar: farolas (y el prisma de la fuente), nodos y
// faroles encendidos, emisores de los rompecabezas.
void Game::gatherLightPoints() {
    lightPoints_.clear();
    for (const Lamp& l : lamps_)
        if (l.on) lightPoints_.push_back({l.pos, {}});
    for (const ColorNode& n : nodes_)
        if (n.lit) lightPoints_.push_back({n.pos + Vec3{0, 1.75f, 0}, {}});
    for (const Lantern& ln : lanterns_)
        if (ln.lit) lightPoints_.push_back({ln.pos + Vec3{0.42f, 1.95f, 0}, {}});
    for (const LightEmitter& em : emitters_) lightPoints_.push_back({em.pos, {}});
    // Se llega junto a la luz, sobre lo que haya debajo (por el lado del Prismero).
    for (LightPoint& p : lightPoints_) {
        Vec3 toHero = normalize(flat(hero_.body.pos - p.pos));
        if (length(toHero) < 0.5f) toHero = -hero_.forward();
        Vec3 probe = p.pos + toHero * 0.3f + Vec3{0, 0.4f, 0};  // desde encima de la luz hacia abajo
        p.arrive = {probe.x, groundHeight(probe), probe.z};
        if (!world_.ground && p.arrive.y <= 0.0f) {  // sobre el vacío: se queda a la altura de la luz
            RayHit h;
            if (!segmentVsWorld(probe, probe - Vec3{0, 30, 0}, world_, h)) p.arrive.y = p.pos.y - 0.5f;
        }
    }
}

// Fuera: la fuente de al lado en la que se puede entrar. Dentro: la elegida con R o la más
// centrada en la cámara, a menos de 30 m y a la vista desde la fuente en la que se está.
void Game::updateLightTarget(const Controls& c) {
    lightTarget_ = -1;
    if (travel_ || hero_.downTimer >= 0) return;
    gatherLightPoints();
    Vec3 chest = hero_.body.pos + Vec3{0, 1.0f, 0};
    RayHit h;
    if (shadowMode_) {  // en modo sombra: desde un charco, otro charco que señale la cámara
        if (hero_.busy() || shadowAt(hero_.body.pos) < 0.5f) return;
        lightPoints_.clear();
        for (const ShadowPool& p : pools_) {  // se llega a un metro del centro, del lado del protagonista
            Vec3 side = normalize(flat(hero_.body.pos - p.pos));
            lightPoints_.push_back({p.pos + Vec3{0, 0.1f, 0}, p.pos + side * 1.0f});
        }
        Vec3 dir = normalize(c.aimDir);
        float best = 1e9f;
        for (size_t i = 0; i < lightPoints_.size(); ++i) {
            const LightPoint& p = lightPoints_[i];
            float d = length(p.pos - hero_.body.pos);
            if (d > kGlideRange || length(flat(p.pos - hero_.body.pos)) < pools_[i].radius * 0.6f) continue;
            Vec3 v = p.pos - c.aimOrigin;
            float along = dot(v, dir);
            if (along <= 0) continue;
            float off = length(v - dir * along);
            if (off > 1.5f + along * 0.12f) continue;
            if (segmentVsWorld(chest, p.arrive + Vec3{0, 1.0f, 0}, world_, h)) continue;
            float score = off / along;
            if (score < best) best = score, lightTarget_ = int(i);
        }
        return;
    }
    if (!inLight_) {
        if (hero_.busy()) return;
        float best = kEnterRange;
        for (size_t i = 0; i < lightPoints_.size(); ++i) {
            const LightPoint& p = lightPoints_[i];
            float d = length(flat(p.pos - hero_.body.pos)), dy = p.pos.y - hero_.body.pos.y;
            if (d > best || dy < -1.0f || dy > 10.0f) continue;
            // A la vista: la luz o, si está en lo alto de un poste o una torre, su pie.
            Vec3 foot{p.pos.x, chest.y, p.pos.z};
            if (segmentVsWorld(chest, p.pos - normalize(p.pos - chest) * 0.4f, world_, h) &&
                (d < 0.7f || segmentVsWorld(chest, foot - normalize(foot - chest) * 0.6f, world_, h)))
                continue;
            best = d, lightTarget_ = int(i);
        }
        return;
    }
    auto visible = [&](Vec3 p) {
        Vec3 dir = normalize(p - hostPos_);
        return !segmentVsWorld(hostPos_ + dir * 0.4f, p - dir * 0.4f, world_, h);
    };
    if (lightLock_) {
        for (size_t i = 0; i < lightPoints_.size(); ++i)
            if (length(lightPoints_[i].pos - lightLockPos_) < 0.1f && visible(lightPoints_[i].pos)) lightTarget_ = int(i);
        if (lightTarget_ >= 0) return;
        lightLock_ = false;  // ya no está (rota, apagada o tapada)
    }
    Vec3 dir = normalize(c.aimDir);
    float best = 1e9f;
    for (size_t i = 0; i < lightPoints_.size(); ++i) {
        const LightPoint& p = lightPoints_[i];
        float d = length(p.pos - hostPos_);
        if (d > kJumpRange || d < 1.0f) continue;
        Vec3 v = p.pos - c.aimOrigin;
        float along = dot(v, dir);
        if (along <= 0) continue;
        float off = length(v - dir * along);
        if (off > 1.0f + along * 0.12f) continue;  // ~7° de margen
        if (!visible(p.pos)) continue;
        float score = off / along;
        if (score < best) best = score, lightTarget_ = int(i);
    }
}

// R dentro de una fuente: pasa a la siguiente fuente alcanzable (de izquierda a derecha
// respecto a la cámara, empezando por la más centrada).
void Game::cycleLightLock(Vec3 camForward) {
    gatherLightPoints();
    Vec3 fwd = normalize(flat(camForward));
    std::vector<std::pair<float, Vec3>> cands;
    RayHit h;
    for (const LightPoint& p : lightPoints_) {
        Vec3 to = p.pos - hostPos_;
        float d = length(to);
        if (d > kJumpRange || d < 1.0f) continue;
        Vec3 dir = to * (1.0f / d);
        if (segmentVsWorld(hostPos_ + dir * 0.4f, p.pos - dir * 0.4f, world_, h)) continue;
        Vec3 f = normalize(flat(to));
        float ang = std::atan2(fwd.x * f.z - fwd.z * f.x, dot(fwd, f));  // ángulo con signo respecto a la cámara
        cands.push_back({ang, p.pos});
    }
    if (cands.empty()) {
        notify("No ves ninguna otra fuente de luz", rgba(200, 200, 215));
        lightLock_ = false;
        return;
    }
    std::sort(cands.begin(), cands.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    size_t pick = 0;
    if (lightLock_) {
        for (size_t i = 0; i < cands.size(); ++i)
            if (length(cands[i].second - lightLockPos_) < 0.1f) pick = (i + 1) % cands.size();
    } else {
        float best = 1e9f;
        for (size_t i = 0; i < cands.size(); ++i)
            if (std::fabs(cands[i].first) < best) best = std::fabs(cands[i].first), pick = i;
    }
    lightLock_ = true;
    lightLockPos_ = cands[pick].second;
    uiSound(Sfx::MenuMove, 0.6f, 1.4f);
}

// El Gris sobre el que caería una emboscada: el más cercano bajo la fuente.
int Game::ambushTarget() const {
    if (!inLight_) return -1;
    int best = -1;
    float bestD = kAmbushRange;
    for (size_t i = 0; i < enemies_.size(); ++i) {
        const Enemy& e = enemies_[i];
        if (!e.targetable()) continue;
        float d = length(flat(e.body.pos - hostPos_));
        if (d < bestD && hostPos_.y - e.body.pos.y < 12.0f) bestD = d, best = int(i);
    }
    return best;
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
    if (inLight_ && p.y < hostPos_.y + 1.0f)  // la fuente ocupada alumbra a su alrededor
        l = std::max(l, 1.0f - smoothstep01((length(flat(p - hostPos_)) - kHostGlow * 0.6f) / (kHostGlow * 0.4f)));
    return l;
}

void Game::updateLight(float dt, const Controls& c) {
    Vec3 chest = hero_.body.pos + Vec3{0, 1.0f, 0};
    float here = lightAt(hero_.body.pos);
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

    // Entrar en la fuente de luz de al lado.
    if (c.light && !hero_.absorbing()) {
        if (lightTarget_ >= 0) {
            startTravel(lightPoints_[size_t(lightTarget_)].pos, 0);
            lock_ = -1;
            sound(Sfx::Echo, hero_.body.pos, 0.7f, 1.6f);
            spawnSparks(chest, {0, 1, 0}, 20, rgba(255, 245, 210), 4.0f);
            return;
        }
        if (lightHint_ <= 0) {
            notify("Acércate a una farola encendida para entrar en su luz", rgba(200, 200, 215));
            lightHint_ = 2.5f;
        }
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

// Dentro de una fuente: invisible e intocable, recupera Luz. Q salta a la fuente elegida,
// Espacio (o esquivar) sale junto a la farola y el clic cae sobre el Gris más cercano.
void Game::updateInsideLight(float dt, const Controls& c) {
    mp_ = std::min(maxMp_, mp_ + kInsideRecharge * dt);
    hero_.setAbsorb(false);
    // La fuente puede apagarse o romperse con el Prismero dentro: sale despedido.
    bool hostOn = false;
    for (const LightPoint& p : lightPoints_) hostOn |= length(p.pos - hostPos_) < 0.1f;
    if (!hostOn && !lightPoints_.empty()) {
        leaveLight(false);
        notify("La luz se ha apagado", rgba(255, 140, 160));
        return;
    }
    if (c.lockOn) cycleLightLock(c.camForward);
    if (c.unlock) lightLock_ = false;
    if (c.light && lightTarget_ >= 0) {
        startTravel(lightPoints_[size_t(lightTarget_)].pos, 0);
        lightLock_ = false;
        sound(Sfx::Echo, hostPos_, 0.6f, 1.8f);
        return;
    }
    if (c.attack && ambushTarget() >= 0) {
        leaveLight(true);
        return;
    }
    if (c.jump || c.dodge || c.attack) leaveLight(false);
    if (random01() < dt * 20.0f && particles_.size() < 900) {
        float a = random01() * 2.0f * kPi;
        particles_.push_back({hostPos_ + Vec3{std::cos(a) * 0.3f, -0.2f, std::sin(a) * 0.3f}, {0, -1.5f, 0}, 0.8f, 0.8f, 0.05f,
                              rgba(255, 240, 200), 0, true});
    }
}

void Game::startTravel(Vec3 to, int mode) {
    travel_ = true;
    travelMode_ = mode;
    travelTime_ = 0;
    travelFrom_ = inLight_ ? hostPos_ - Vec3{0, 1.0f, 0} : hero_.body.pos;
    travelTo_ = mode == 0 ? to - Vec3{0, 1.0f, 0} : to;  // al entrar, el pecho queda en la luz
    travelDur_ = (mode == 0 ? 0.2f : 0.15f) + length(travelTo_ - travelFrom_) / 50.0f;
    inLight_ = false;
    hero_.setGuard(false);
}

// Salir de la fuente: junto a ella, deslumbrando a los Grises de alrededor; o, en
// emboscada, en picado sobre el Gris más cercano.
void Game::leaveLight(bool ambush) {
    int target = ambush ? ambushTarget() : -1;
    lightLock_ = false;
    if (target >= 0) {
        const Enemy& e = enemies_[size_t(target)];
        Vec3 dir = normalize(flat(e.body.pos - hostPos_));
        if (length(dir) < 0.5f) dir = hero_.forward();
        ambushEnemy_ = target;
        hero_.yaw = std::atan2(dir.x, dir.z);
        startTravel(e.body.pos - dir * (e.body.radius + 0.6f), 2);
        sound(Sfx::Dodge, hostPos_, 1.0f, 0.7f);
        return;
    }
    Vec3 out;
    bool found = false;
    for (const LightPoint& p : lightPoints_)
        if (length(p.pos - hostPos_) < 0.1f) out = p.arrive, found = true;
    if (!found) out = Vec3{hostPos_.x, groundHeight(hostPos_ + Vec3{0, 0.4f, 0}), hostPos_.z};
    startTravel(out + Vec3{0, 0.05f, 0}, 1);
}

// Durante el viaje el Prismero es luz que vuela en arco (atraviesa lo que haya en medio).
void Game::updateTravel(float dt) {
    if (inLight_) {  // dentro: quieto en la fuente
        hero_.body.pos = hostPos_ - Vec3{0, 1.0f, 0};
        hero_.body.vel = {};
        return;
    }
    if (!travel_) return;
    travelTime_ += dt;
    float u = clampf(travelTime_ / travelDur_, 0, 1);
    float e = smoothstep01(u);
    float arc = travelMode_ == 0 ? std::min(3.0f, length(travelTo_ - travelFrom_) * 0.15f) : travelMode_ == 3 ? 0.0f : 0.4f;
    hero_.body.pos = travelFrom_ + (travelTo_ - travelFrom_) * e + Vec3{0, std::sin(u * kPi) * arc, 0};
    hero_.body.vel = {};
    if (random01() < dt * 60.0f && particles_.size() < 900) {
        if (travelMode_ == 3)  // por el suelo, hecho sombra
            particles_.push_back({hero_.body.pos + Vec3{0, 0.1f, 0}, {0, 0.5f, 0}, 0.6f, 0.6f, 0.14f, rgba(30, 16, 48), 0, false});
        else
            particles_.push_back({hero_.body.pos + Vec3{0, 1, 0}, {0, 0.2f, 0}, 0.4f, 0.4f, 0.08f, rgba(255, 240, 200), 0, true});
    }
    if (u < 1.0f) return;
    travel_ = false;
    if (travelMode_ == 3) {  // emerge de la sombra
        hero_.body.pos = travelTo_ + Vec3{0, 0.05f, 0};
        spawnRing(travelTo_ + Vec3{0, 0.05f, 0}, 1.8f, 0.5f, rgba(150, 80, 255));
        sound(Sfx::Dodge, travelTo_, 0.6f, 0.5f);
        return;
    }
    Vec3 chest = travelTo_ + Vec3{0, 1.0f, 0};
    if (travelMode_ == 0) {  // dentro de la fuente
        inLight_ = true;
        hostPos_ = chest;
        hero_.body.pos = travelTo_;
        spawnSparks(chest, {0, 1, 0}, 16, rgba(255, 245, 210), 3.0f);
        sound(Sfx::PerfectGuard, chest, 0.4f, 1.5f);
        return;
    }
    hero_.body.pos = travelTo_;
    spawnRing(travelTo_ + Vec3{0, 0.05f, 0}, travelMode_ == 2 ? 3.0f : 2.0f, 0.5f, rgba(255, 240, 200));
    spawnSparks(chest, {0, 1, 0}, 24, rgba(255, 245, 210), 5.0f);
    if (travelMode_ == 2) {  // emboscada: golpe fuerte al objetivo y a lo que haya cerca
        const WeaponStats& w = inv_.weaponStats();
        float damage = (w.power + inv_.powerBonus) * 2.5f;
        for (Enemy& en : enemies_) {
            if (!en.targetable()) continue;
            Vec3 to = flat(en.body.pos - travelTo_);
            if (length(to) > 2.8f + en.body.radius) continue;
            Vec3 dir = length(to) > 0.1f ? normalize(to) : hero_.forward();
            hitEnemy(en, dir, damage, 6.0f, 4.0f, true);
        }
        shake_ = std::max(shake_, 0.45f);
        hitStop_ = 0.1f;
        sound(Sfx::FireBoom, chest, 0.9f, 1.2f);
        ambushEnemy_ = -1;
        return;
    }
    // Al salir, el destello deslumbra a los Grises de alrededor.
    for (Enemy& en : enemies_)
        if (en.targetable() && !en.boss && length(flat(en.body.pos - travelTo_)) < 3.5f) {
            en.state = AiState::Stagger;
            en.stateTime = 0;
        }
    sound(Sfx::PerfectGuard, chest, 0.5f, 1.3f);
}
