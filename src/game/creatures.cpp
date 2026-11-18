// Criaturas del bestiario (ver assets/models/bestiario/FICHA_BESTIARIO.md): dónde vive cada
// una, qué hace y cómo se mueve. No pelean ni se las puede herir; los Grises no las ven.
// La animación (actors/creature.cpp) es por rotaciones locales de sus huesos, como sugiere la ficha.

#include "game/game.h"

#include <algorithm>
#include <cmath>

namespace {

float rnd(uint32_t& s) {
    s = s * 1664525u + 1013904223u;
    return float(s >> 8) / 16777216.0f;
}

constexpr float kCatFlee = 3.0f;    // más cerca, el gato salta a otro tejado
constexpr float kWhaleRadius = 70;  // la vuelta de la ballena (en la plaza, menos)

}  // namespace

void Game::addCreature(CreatureKind kind, Vec3 pos, float yaw) {
    if (!creatureRig(kind).loaded()) return;  // sin su .glb no sale
    Creature c;
    c.kind = kind;
    c.pos = c.home = pos;
    c.yaw = yaw;
    c.seed = uint32_t(creatures_.size()) * 2246822519u + uint32_t(kind) * 97u + 7u;
    c.timer = rnd(c.seed) * 4.0f;
    c.phase = rnd(c.seed) * 6.0f;
    c.body.pos = pos;
    c.body.radius = kind == CreatureKind::Larguero ? 0.45f : 0.5f;
    c.body.height = kind == CreatureKind::Larguero ? 3.2f : 1.2f;
    switch (kind) {
        case CreatureKind::Topo:
            c.lines = {"Cristal, cuerda, aceite. Lo que haga falta ahí abajo.", "Escarbo hacia la luz. Algún día llego."};
            break;
        case CreatureKind::Caracol:
            c.lines = {"Despacio, despacio. Lo bueno llega tarde.", "Frascos de luz embotellada. Del último atardecer."};
            break;
        case CreatureKind::Larguero: c.lines = {"Una más. Siempre queda una más.", "Desde aquí arriba se ven todas. Casi."}; break;
        default: break;
    }
    creatures_.push_back(c);
    animateCreature(creatures_.back());
    std::copy(std::begin(creatures_.back().joints), std::end(creatures_.back().joints), std::begin(creatures_.back().prevJoints));
}

bool Game::lampSwitchedOff(int lamp) const {
    for (const LampSwitch& s : switches_)
        if (!s.on && std::find(s.lamps.begin(), s.lamps.end(), lamp) != s.lamps.end()) return true;
    return false;
}

// Tejados: cajas sólidas de 2,5 a 20 m de alto, con sitio para andar y nada encima.
std::vector<AABB> Game::roofSpots() const {
    std::vector<AABB> out;
    auto consider = [&](const AABB& b) {
        Vec3 h = b.halfExtents(), c = b.center();
        if (h.x < 0.9f || h.z < 0.9f || h.x > 30 || h.z > 30) return;
        float tall = b.max.y - world_.groundAt(c.x, c.z);
        if (tall < 2.5f || tall > 20) return;
        RayHit hit;
        Vec3 top{c.x, b.max.y + 0.05f, c.z};
        if (segmentVsWorld(top + Vec3{0, 0.05f, 0}, top + Vec3{0, 3, 0}, world_, hit)) return;  // tapado
        out.push_back(b);
    };
    for (const Block& b : blocks_) consider(b.box);
    for (const AABB& b : propSolids_) consider(b);
    return out;
}

void Game::placeCreatures() {
    creatures_.clear();
    roofs_.clear();
    // La Mechita va con Vela a todas partes.
    addCreature(CreatureKind::Mechita, heroStart_ + Vec3{std::cos(heroStartYaw_), 0, -std::sin(heroStartYaw_)} * 0.8f, heroStartYaw_);
    bool open = world_.terrain != nullptr;
    // Gatos en los tejados de la ciudad: el más cercano a la llegada que no esté pegado a ella.
    if (level_ == LevelId::Plaza || level_ == LevelId::Callejones || level_ == LevelId::Mercado || level_ == LevelId::Neon ||
        level_ == LevelId::Rio || level_ == LevelId::Cafetero) {
        roofs_ = roofSpots();
        std::vector<AABB> roofs = roofs_;
        std::sort(roofs.begin(), roofs.end(), [&](const AABB& a, const AABB& b) {
            return length(flat(a.center() - heroStart_)) < length(flat(b.center() - heroStart_));
        });
        int want = level_ == LevelId::Callejones ? 2 : 1, placed = 0;
        for (const AABB& r : roofs) {
            if (placed >= want) break;
            Vec3 c = r.center();
            if (length(flat(c - heroStart_)) < 8.0f) continue;
            addCreature(CreatureKind::Hollinete, {c.x, r.max.y, c.z}, std::atan2(heroStart_.x - c.x, heroStart_.z - c.z));
            if (!creatures_.empty() && creatures_.back().kind == CreatureKind::Hollinete) creatures_.back().area = r, ++placed;
        }
    }
    // Búhos en las farolas: las de 10 a 35 m de la llegada, uno cada pocas.
    {
        std::vector<int> lamps;
        for (size_t i = 0; i < lamps_.size(); ++i) {
            const Lamp& l = lamps_[i];
            float d = length(flat(l.pos - heroStart_));
            if ((l.style == LampStyle::Post || l.style == LampStyle::Prop) && l.bulb && d > 10 && d < 35) lamps.push_back(int(i));
        }
        std::sort(lamps.begin(), lamps.end(), [&](int a, int b) {
            return length(flat(lamps_[size_t(a)].pos - heroStart_)) < length(flat(lamps_[size_t(b)].pos - heroStart_));
        });
        int want = open ? 2 : 1;
        for (size_t k = 0; k < lamps.size() && want > 0; k += 3, --want) {
            const Lamp& l = lamps_[size_t(lamps[k])];
            addCreature(CreatureKind::Buho, l.pos + Vec3{0, 0.3f, 0}, std::atan2(heroStart_.x - l.pos.x, heroStart_.z - l.pos.z));
            if (!creatures_.empty() && creatures_.back().kind == CreatureKind::Buho) creatures_.back().lamp = lamps[k];
        }
    }
    // Comerciantes: el topo en los túneles de la cueva, el caracol en el mercado.
    auto onFloor = [&](Vec3 p) {
        if (!devFreeSpot(p, 0.8f)) return p;
        p.y = groundHeight(p + Vec3{0, 2, 0});
        return p;
    };
    if (level_ == LevelId::Cueva) addCreature(CreatureKind::Topo, onFloor({5.5f, 0, 5}), -2.4f);
    if (level_ == LevelId::Mercado) addCreature(CreatureKind::Caracol, onFloor(heroStart_ + Vec3{-5, 0, 12}), 2.0f);
    // El Larguero, por los mapas grandes, donde las polillas apagan farolas.
    if (open) addCreature(CreatureKind::Larguero, onFloor(heroStart_ + Vec3{14, 0, 18}), 0);
    // La ballena, en el cielo de los sitios abiertos.
    if (open || level_ == LevelId::Plaza || level_ == LevelId::Puente) {
        Vec3 c = heroStart_ + Vec3{0, open ? 34.0f : 24.0f, 0};
        addCreature(CreatureKind::Ballena, c, 0);
    }
}

// --- Comportamiento ------------------------------------------------------------------------

void Game::updateCreatures(float dt) {
    for (Creature& c : creatures_) {
        std::copy(std::begin(c.joints), std::end(c.joints), std::begin(c.prevJoints));
        Vec3 toHero = flat(hero_.body.pos - c.pos);
        float dh = length(toHero);
        float heroYaw = std::atan2(toHero.x, toHero.z);
        c.timer -= dt;
        c.speed = 0;
        switch (c.kind) {
            case CreatureKind::Mechita: {
                // Junto a Vela, un paso detrás y a un lado; con ella dentro de una farola, fundida
                // con la sombra o caída, se apaga y la espera.
                bool gone = inLight_ || travel_ || diving_ || hero_.downTimer >= 0;
                c.hide = moveTowards(c.hide, gone ? 1.0f : 0.0f, dt * 4.0f);
                Vec3 right{std::cos(hero_.yaw), 0, -std::sin(hero_.yaw)};
                Vec3 spot = hero_.body.pos + right * 0.8f - hero_.forward() * 0.6f;
                Vec3 d = flat(spot - c.pos);
                float far = length(d);
                if (c.hide > 0.99f || far > 14.0f || std::fabs(hero_.body.pos.y - c.pos.y) > 3.0f) {  // reaparece a su lado
                    c.pos = spot;
                    far = 0;
                }
                if (far > 0.35f) {
                    c.speed = std::min(7.5f, far * 2.5f);
                    c.pos += normalize(d) * std::min(far, c.speed * dt);
                    c.yaw = wrapAngle(c.yaw + clampf(wrapAngle(std::atan2(d.x, d.z) - c.yaw), -10 * dt, 10 * dt));
                } else {  // quieta: mira a donde mira Vela
                    c.yaw = wrapAngle(c.yaw + clampf(wrapAngle(hero_.yaw - c.yaw), -3 * dt, 3 * dt));
                }
                c.pos.y = groundHeight(c.pos + Vec3{0, 1.2f, 0});
                c.phase += (c.speed > 0 ? 10.0f + c.speed : 0.0f) * dt;
                c.lookTarget = clampf(wrapAngle(heroYaw - c.yaw), -1.0f, 1.0f) * (dh > 0.3f ? 1.0f : 0.0f);
                break;
            }
            case CreatureKind::Hollinete: {
                if (c.hop >= 0) {  // saltando de tejado en tejado
                    c.hop += dt;
                    float u = std::min(1.0f, c.hop / c.hopTime);
                    c.pos = c.from + (c.to - c.from) * u + Vec3{0, std::sin(u * kPi) * (1.5f + 0.15f * length(c.to - c.from)), 0};
                    c.phase += 14.0f * dt;
                    if (u >= 1) c.hop = -1, c.timer = 3.0f + rnd(c.seed) * 4.0f;
                    break;
                }
                bool close = dh < kCatFlee && std::fabs(hero_.body.pos.y - c.pos.y) < 2.5f;
                if (close) {  // otro tejado a un salto (si no hay, se queda y la mira)
                    const AABB* best = nullptr;
                    float bestD = 1e9f;
                    for (const AABB& r : roofs_) {
                        Vec3 rc{r.center().x, r.max.y, r.center().z};
                        float d = length(rc - c.pos);
                        if (d > 3.0f && d < 16.0f && length(flat(rc - hero_.body.pos)) > 5.0f && d < bestD) bestD = d, best = &r;
                    }
                    if (best) {
                        c.area = *best;
                        c.from = c.pos;
                        c.to = {best->center().x, best->max.y, best->center().z};
                        c.hop = 0;
                        c.hopTime = 0.6f + 0.04f * bestD;
                        c.yaw = std::atan2(c.to.x - c.from.x, c.to.z - c.from.z);
                        break;
                    }
                }
                if (c.timer <= 0) {  // a otro punto de su tejado
                    Vec3 lo = c.area.min, hi = c.area.max;
                    c.to = {lerpf(lo.x + 0.4f, hi.x - 0.4f, rnd(c.seed)), hi.y, lerpf(lo.z + 0.4f, hi.z - 0.4f, rnd(c.seed))};
                    c.timer = 6.0f + rnd(c.seed) * 8.0f;
                }
                Vec3 d = flat(c.to - c.pos);
                if (c.timer < 4.0f || length(d) < 0.2f) {  // sentado
                    c.lookTarget = dh < 14.0f ? clampf(wrapAngle(heroYaw - c.yaw), -1.3f, 1.3f) : 0.4f * std::sin(time_ * 0.3f + c.phase);
                } else {
                    c.speed = 0.9f;
                    c.pos += normalize(d) * std::min(length(d), c.speed * dt);
                    c.yaw = wrapAngle(c.yaw + clampf(wrapAngle(std::atan2(d.x, d.z) - c.yaw), -5 * dt, 5 * dt));
                    c.phase += 7.0f * dt;
                    c.lookTarget = 0;
                }
                break;
            }
            case CreatureKind::Buho: {
                if (c.hop >= 0) {  // volando a otra farola
                    c.hop += dt;
                    float u = smoothstep01(std::min(1.0f, c.hop / c.hopTime));
                    c.pos = c.from + (c.to - c.from) * u + Vec3{0, std::sin(u * kPi) * 2.5f, 0};
                    c.phase += 16.0f * dt;
                    if (c.hop >= c.hopTime) c.hop = -1, c.timer = 1.0f;
                    break;
                }
                bool dark = c.lamp < 0 || c.lamp >= int(lamps_.size()) || !lamps_[size_t(c.lamp)].on || lamps_[size_t(c.lamp)].broken;
                if (dark && c.timer <= 0) {  // le han apagado la farola: a la encendida más cercana
                    int best = -1;
                    float bestD = 40.0f;
                    for (size_t i = 0; i < lamps_.size(); ++i) {
                        const Lamp& l = lamps_[i];
                        float d = length(l.pos - c.pos);
                        bool taken = false;
                        for (const Creature& o : creatures_) taken |= &o != &c && o.kind == CreatureKind::Buho && o.lamp == int(i);
                        if (l.on && !l.broken && l.bulb && !taken && d > 1.0f && d < bestD &&
                            (l.style == LampStyle::Post || l.style == LampStyle::Prop))
                            bestD = d, best = int(i);
                    }
                    c.timer = 2.0f;  // si no hay ninguna, vuelve a mirar dentro de un rato
                    if (best >= 0) {
                        c.lamp = best;
                        c.from = c.pos;
                        c.to = lamps_[size_t(best)].pos + Vec3{0, 0.3f, 0};
                        c.hop = 0;
                        c.hopTime = 1.0f + 0.06f * bestD;
                        c.yaw = std::atan2(c.to.x - c.from.x, c.to.z - c.from.z);
                        break;
                    }
                }
                // Autómata: la cabeza gira a golpes secos, hacia Vela o a cualquier lado.
                if (c.pointTime > 0) {
                    c.pointTime -= dt;
                    Vec3 p = flat(c.pointAt - c.pos);
                    c.lookTarget = clampf(wrapAngle(std::atan2(p.x, p.z) - c.yaw), -2.1f, 2.1f);
                } else if (c.timer <= 0) {
                    c.timer = 1.2f + rnd(c.seed) * 2.0f;
                    c.lookTarget = dh < 20.0f && rnd(c.seed) < 0.7f ? clampf(wrapAngle(heroYaw - c.yaw), -2.1f, 2.1f)
                                                                     : (rnd(c.seed) * 2 - 1) * 2.1f;
                }
                break;
            }
            case CreatureKind::Topo: {
                // Escarba; si Vela se acerca, para y la mira.
                c.lookTarget = dh < 5.0f ? clampf(wrapAngle(heroYaw - c.yaw), -1.0f, 1.0f) : 0.0f;
                c.hide = moveTowards(c.hide, dh < 5.0f ? 1.0f : 0.0f, dt * 2.0f);  // aquí: 1 = atento (deja de cavar)
                break;
            }
            case CreatureKind::Caracol: {
                // Se mete en la concha con Grises cerca o si se le echan encima.
                bool scared = dh < 0.95f;
                for (const Enemy& e : enemies_) scared |= e.alive() && !isSwarm(e.kind) && length(flat(e.body.pos - c.pos)) < 12.0f;
                c.hide = moveTowards(c.hide, scared ? 1.0f : 0.0f, dt * (scared ? 3.0f : 0.6f));
                Vec3 dir{};
                if (c.hide < 0.05f) {
                    if (c.timer <= 0) {  // otro rincón de su zona
                        float a = rnd(c.seed) * 2 * kPi, r = std::sqrt(rnd(c.seed)) * 4.0f;
                        c.to = c.home + Vec3{std::cos(a) * r, 0, std::sin(a) * r};
                        c.timer = 14.0f + rnd(c.seed) * 10.0f;
                    }
                    Vec3 d = flat(c.to - c.body.pos);
                    if (dh < 4.0f) {  // atiende a Vela
                        c.lookTarget = clampf(wrapAngle(heroYaw - c.yaw), -0.9f, 0.9f);
                    } else if (length(d) > 0.3f && c.timer < 12.0f) {
                        dir = normalize(d);
                        c.speed = 0.3f;
                        c.yaw = wrapAngle(c.yaw + clampf(wrapAngle(std::atan2(d.x, d.z) - c.yaw), -0.8f * dt, 0.8f * dt));
                        c.lookTarget = 0;
                    }
                }
                c.body.vel.x = dir.x * c.speed;
                c.body.vel.z = dir.z * c.speed;
                moveBody(c.body, world_, dt, 0.25f);
                c.pos = c.body.pos;
                c.phase += c.speed * 6.0f * dt;
                break;
            }
            case CreatureKind::Larguero: {
                // Va a la farola apagada más cercana (rota no, ni la que tiene el interruptor
                // abajo), alza el farol y la enciende. Si se atasca, prueba con otra.
                if (c.lighting >= 0) {
                    c.lighting += dt;
                    if (c.lamp >= 0 && c.lamp < int(lamps_.size()) && c.lighting >= 1.1f && !lamps_[size_t(c.lamp)].on) {
                        Lamp& l = lamps_[size_t(c.lamp)];
                        l.on = true;
                        l.energy = 1;
                        spawnSparks(l.pos, {0, 1, 0}, 18, rgba(255, 214, 140), 2.5f);
                        sound(Sfx::Echo, l.pos, 0.5f, 1.4f);
                        if (length(flat(l.pos - hero_.body.pos)) < 40.0f) notify("El Larguero ha vuelto a encender una farola", rgba(255, 214, 140));
                    }
                    if (c.lighting > 2.2f) c.lighting = -1, c.lamp = -1, c.timer = 2.0f;
                    break;
                }
                if (c.lamp < 0 && c.timer <= 0) {
                    float best = 70.0f;
                    for (size_t i = 0; i < lamps_.size(); ++i) {
                        const Lamp& l = lamps_[i];
                        float d = length(flat(l.pos - c.pos));
                        if (!l.on && !l.broken && l.bulb && d < best && !lampSwitchedOff(int(i))) best = d, c.lamp = int(i);
                    }
                    c.timer = c.lamp >= 0 ? 40.0f : 3.0f;  // el tiempo que se da para llegar
                    if (c.lamp < 0) {  // nada que encender: pasea cerca de casa
                        float a = rnd(c.seed) * 2 * kPi, r = std::sqrt(rnd(c.seed)) * 14.0f;
                        c.to = c.home + Vec3{std::cos(a) * r, 0, std::sin(a) * r};
                        c.timer = 12.0f + rnd(c.seed) * 8.0f;
                    }
                }
                if (c.lamp >= 0 && (c.lamp >= int(lamps_.size()) || lamps_[size_t(c.lamp)].on || c.timer <= 0)) c.lamp = -1;  // otro la encendió, o no llega
                Vec3 goal = c.lamp >= 0 ? lamps_[size_t(c.lamp)].pos : c.to;
                Vec3 d = flat(goal - c.body.pos);
                Vec3 dir{};
                if (c.lamp >= 0 && length(d) < 1.6f) {
                    c.lighting = 0;
                    c.yaw = std::atan2(d.x, d.z);
                } else if (length(d) > 0.8f && (c.lamp >= 0 || c.timer < 9.0f)) {
                    dir = normalize(d);
                    c.speed = 1.3f;
                    c.yaw = wrapAngle(c.yaw + clampf(wrapAngle(std::atan2(d.x, d.z) - c.yaw), -1.5f * dt, 1.5f * dt));
                }
                c.lookTarget = dh < 8.0f ? clampf(wrapAngle(heroYaw - c.yaw), -0.8f, 0.8f) : 0.0f;
                c.body.vel.x = dir.x * c.speed;
                c.body.vel.z = dir.z * c.speed;
                moveBody(c.body, world_, dt, 0.5f);
                c.pos = c.body.pos;
                c.phase += c.speed * 2.2f * dt;  // zancadas largas y lentas
                break;
            }
            case CreatureKind::Ballena: {
                // Una vuelta lenta y alta; sube y baja un poco, como si respirara.
                float radius = level_ == LevelId::Plaza || level_ == LevelId::Puente ? kWhaleRadius * 0.6f : kWhaleRadius;
                c.phase += dt * 2.6f / radius;  // ~2,6 m/s
                float a = c.phase;
                c.pos = c.home + Vec3{std::cos(a) * radius, 2.0f * std::sin(time_ * 0.25f + float(c.seed % 7)), std::sin(a) * radius};
                c.yaw = std::atan2(-std::sin(a), std::cos(a));  // tangente a la vuelta
                c.speed = 2.6f;
                break;
            }
            case CreatureKind::Count: break;
        }
        // La cabeza: los autómatas a golpes, los demás suave.
        float rate = c.kind == CreatureKind::Buho ? 14.0f : 4.0f;
        c.look += clampf(c.lookTarget - c.look, -rate * dt, rate * dt);
        animateCreature(c);
    }
}

void Game::talkToCreature(Creature& c) {
    std::string text = c.lines.empty() ? std::string() : c.lines[size_t(c.line) % c.lines.size()];
    ++c.line;
    switch (c.kind) {
        case CreatureKind::Buho: {
            // Señala con la cabeza al Gris más cercano; si no hay, cuenta las farolas apagadas.
            const Enemy* closest = nullptr;
            float best = 70.0f;
            int count = 0;
            for (const Enemy& e : enemies_) {
                if (!e.alive() || isSwarm(e.kind)) continue;
                float d = length(flat(e.body.pos - c.pos));
                if (d < 70.0f) ++count;
                if (d < best) best = d, closest = &e;
            }
            if (closest) {
                c.pointAt = closest->body.pos;
                c.pointTime = 5.0f;
                text = count == 1 ? "Uuh. Uno. Hacia donde miro." : "Uuh. " + std::to_string(count) + " Grises. Hacia donde miro.";
                spawnSparks(c.pos + Vec3{0, 0.6f, 0}, normalize(c.pointAt - c.pos), 12, rgba(255, 214, 140), 4.0f);
            } else {
                int dark = 0;
                for (const Lamp& l : lamps_) dark += (!l.on || l.broken) && length(flat(l.pos - c.pos)) < 40.0f;
                text = dark == 0 ? "Uuh. Todo encendido. Todo tranquilo." : "Uuh. " + std::to_string(dark) + (dark == 1 ? " farola apagada cerca." : " farolas apagadas cerca.");
            }
            sound(Sfx::Blip, c.pos, 0.6f, 0.6f);
            break;
        }
        case CreatureKind::Topo:
        case CreatureKind::Caracol:
            shopRequest_ = true;
            if (c.kind == CreatureKind::Caracol) c.hide = 0;
            break;
        default: break;
    }
    if (!text.empty()) {
        npcSpeaker_ = creatureName(c.kind);
        npcLine_ = text;
        npcLineTime_ = 5.0f;
    }
}

// --- Animación (actors/creature.cpp) ----------------------------------------------------------

void Game::animateCreature(Creature& c) {
    CreatureMotion m;
    m.phase = c.phase;
    m.speed = c.speed;
    m.look = c.look;
    m.hide = c.hide;
    m.hop = c.hop >= 0 ? std::min(1.0f, c.hop / c.hopTime) : -1.0f;
    m.lighting = c.lighting;
    m.time = time_ + float(c.seed % 1000) * 0.01f;
    const CreatureRig& rig = creatureRig(c.kind);
    poseCreature(c.kind, rig, m, c.pose);
    float sc = creatureScale(c.kind) * (c.kind == CreatureKind::Mechita ? std::max(0.001f, 1.0f - smoothstep01(c.hide)) : 1.0f);
    Mat4 model = translate(c.pos) * rotateY(c.yaw) * scale({sc, sc, sc});
    computeWorldMatrices(rig.joints, rig.count, c.pose, model, c.joints);
}
