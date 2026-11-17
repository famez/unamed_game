// Enjambres: polillas de tinta y luciérnagas de cristal (ver la biblia de personajes).
//   - Polilla de tinta (sombra): llegan en grupos a la farola encendida más cercana y la
//     rondan; si nadie se acerca a espantarlas en 10 s, la apagan. Mueren de un golpe.
//   - Luciérnaga de cristal (luz): chispas que se escapan de las farolas rotas. Rápidas y
//     agresivas: se lanzan contra la protagonista. Al morir estallan y encienden lo que tocan
//     (las farolas apagadas cercanas) y hieren a los Grises de sombra de alrededor.
// Vuelan sin chocar (son pequeñas) y no aprenden: no tienen cerebro.

#include "game/game.h"

#include <algorithm>
#include <cmath>

namespace {

constexpr float kMothPutOut = 10.0f;  // segundos rondando una farola hasta apagarla
constexpr float kScare = 3.5f;        // la protagonista a esta distancia de la farola las espanta
constexpr float kDash = 0.35f;        // luciérnaga: lo que dura la embestida

}  // namespace

void Game::updateSwarm(Enemy& e, float dt) {
    const EnemyDef& d = e.def();
    e.phase += 14.0f * dt;
    if (e.state == AiState::Spawning) {
        if (e.stateTime > 0.5f) e.state = AiState::Idle, e.stateTime = 0;
    }
    if (e.state == AiState::Dying) {
        animateEnemy(e, time_, dt);
        return;
    }
    Vec3 pos = e.center();
    Vec3 chest = hero_.body.pos + Vec3{0, 1.1f, 0};
    Vec3 goal;
    float speed = d.speed;
    if (e.kind == EnemyKind::Moth) {
        // Su farola: la encendida más cercana (si se la apagan o se rompe, busca otra).
        if (e.swarmLamp >= 0 && (e.swarmLamp >= int(lamps_.size()) || !lamps_[size_t(e.swarmLamp)].on ||
                                 lamps_[size_t(e.swarmLamp)].broken))
            e.swarmLamp = -1, e.swarmTime = 0;
        if (e.swarmLamp < 0) {
            float best = 45.0f;
            for (size_t i = 0; i < lamps_.size(); ++i) {
                const Lamp& l = lamps_[i];
                float dl = length(l.pos - pos);
                if (l.on && !l.broken && l.bulb && dl < best) best = dl, e.swarmLamp = int(i);
            }
        }
        float a = time_ * (1.6f + std::fmod(e.seed, 1.0f)) + e.seed * 6.0f;
        if (e.swarmLamp >= 0) {
            Lamp& l = lamps_[size_t(e.swarmLamp)];
            bool scared = hero_.downTimer < 0 && length(flat(hero_.body.pos - l.pos)) < kScare;
            if (scared) e.cooldown = 1.5f;  // espantadas: se apartan un rato y vuelven a empezar
            if (e.cooldown > 0) {
                Vec3 away = flat(pos - l.pos);
                away = length(away) > 0.1f ? normalize(away) : Vec3{std::cos(a), 0, std::sin(a)};
                goal = l.pos + away * 5.0f + Vec3{0, 1.0f, 0};
                e.swarmTime = 0;
            } else {
                float r = 0.55f + 0.35f * std::sin(e.seed * 3.1f);  // revolotean alrededor del cristal
                goal = l.pos + Vec3{std::cos(a) * r, 0.25f * std::sin(time_ * 3.0f + e.seed), std::sin(a) * r};
                if (length(goal - pos) < 1.5f) e.swarmTime += dt;
                if (e.swarmTime >= kMothPutOut) {  // la apagan (no la rompen: el interruptor la devuelve)
                    l.on = false;
                    if (hostLamp_ == e.swarmLamp && inLight_) leaveLight(false);
                    spawnSparks(l.pos, {0, -1, 0}, 14, rgba(150, 80, 255), 2.5f);
                    sound(Sfx::Ice, l.pos, 0.6f, 0.6f);
                    if (length(flat(l.pos - hero_.body.pos)) < 40.0f) notify("Las polillas han apagado una farola", rgba(190, 140, 255));
                    for (Enemy& o : enemies_)
                        if (o.kind == EnemyKind::Moth && o.swarmLamp == e.swarmLamp) o.swarmLamp = -1, o.swarmTime = 0;
                }
            }
        } else {  // sin farolas: rondan a la protagonista, por encima
            goal = chest + Vec3{std::cos(a) * 3.0f, 1.2f + 0.3f * std::sin(time_ * 2.0f + e.seed), std::sin(a) * 3.0f};
        }
    } else {  // luciérnaga: rodea a la protagonista y se lanza contra ella
        float a = time_ * 2.2f + e.seed * 6.0f;
        bool canSee = hero_.downTimer < 0 && !inLight_ && !travel_ && !diving_;
        if (e.state == AiState::Attack) {
            if (e.stateTime > kDash) e.state = AiState::Recover, e.stateTime = 0, e.cooldown = 1.2f + random01();
            goal = pos + e.knock;  // sigue la embestida en línea recta
            speed = 15.0f;
            if (!e.attackHit && length(chest - pos) < 0.7f && canSee) {
                e.attackHit = true;
                hurtHero(normalize(flat(chest - pos) + Vec3{0, 0.1f, 0}), d.damage, d.knock, &e);
                spawnSparks(pos, {0, 1, 0}, 8, rgba(255, 240, 200), 2.5f);
            }
        } else {
            if (e.state == AiState::Recover && e.stateTime > 0.3f) e.state = AiState::Idle, e.stateTime = 0;
            goal = canSee ? chest + Vec3{std::cos(a) * 2.0f, 0.3f * std::sin(time_ * 4.0f + e.seed) - 0.2f, std::sin(a) * 2.0f}
                          : pos + Vec3{std::cos(a), 0.2f, std::sin(a)};
            if (canSee && e.cooldown <= 0 && e.state == AiState::Idle && length(chest - pos) < 5.5f) {
                e.state = AiState::Attack;
                e.stateTime = 0;
                e.attackHit = false;
                e.knock = normalize(chest - pos) * 15.0f;  // dirección de la embestida
                sound(Sfx::Slash, pos, 0.4f, 1.8f);
            }
        }
    }
    // Vuelo: se acercan al punto con un muelle (sin chocar con nada) y sin bajar del suelo.
    Vec3 to = goal - pos;
    Vec3 want = length(to) > 1e-3f ? to * std::min(1.0f, speed / std::max(length(to), 1e-3f)) * (e.state == AiState::Attack ? 1.0f : 2.5f)
                                   : Vec3{};
    if (length(want) > speed) want = normalize(want) * speed;
    e.body.vel = e.body.vel + (want - e.body.vel) * std::min(1.0f, dt * 6.0f);
    e.body.pos += e.body.vel * dt;
    float floor = world_.groundAt(e.body.pos.x, e.body.pos.z) + 0.3f;
    if (e.body.pos.y < floor) e.body.pos.y = floor, e.body.vel.y = std::max(0.0f, e.body.vel.y);
    if (length(flat(e.body.vel)) > 0.3f) e.yaw = std::atan2(e.body.vel.x, e.body.vel.z);
    animateEnemy(e, time_, dt);
}

void Game::fireflyBurst(Vec3 c) {
    spawnRing(c, 3.5f, 0.5f, rgba(255, 220, 140));
    spawnSparks(c, {0, 1, 0}, 26, rgba(255, 236, 170), 6.0f);
    sound(Sfx::FireBoom, c, 0.5f, 1.7f);
    for (Lamp& l : lamps_)  // enciende lo que toca: las farolas apagadas (las rotas ya no)
        if (!l.on && !l.broken && length(l.pos - c) < 3.5f) {
            l.on = true;
            l.energy = 1;
            spawnSparks(l.pos, {0, 1, 0}, 12, l.color, 3.0f);
        }
    for (size_t i = 0; i < enemies_.size(); ++i) {  // y quema a los de sombra que tiene cerca
        Enemy& o = enemies_[i];
        if (!o.alive() || !o.def().shadow || length(o.center() - c) > 3.0f) continue;
        o.health -= 15.0f;
        o.hitFlash = 0.15f;
        if (o.health <= 0) killEnemy(o);
    }
}
