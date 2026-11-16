// Habitantes de los mapas grandes: pasean por su zona a paso tranquilo (con el mismo
// Character que la protagonista, así andan y se paran con sus animaciones), descansan un
// rato y, si la protagonista se les acerca, se paran y la miran. Los lejanos no se mueven
// (no se ven) para no gastar.

#include "game/game.h"

#include <algorithm>
#include <cmath>

namespace {

constexpr float kActiveRange = 90.0f;  // más lejos, quietos
constexpr float kWalk = 0.3f;          // fracción de la carrera: paseo
constexpr float kLook = 3.5f;          // a esta distancia se paran a mirarla

float rnd(uint32_t& s) {
    s = s * 1664525u + 1013904223u;
    return float(s >> 8) / 16777216.0f;
}

}  // namespace

void Game::addNpc(const std::string& model, Vec3 pos, float radius) {
    Npc n;
    n.model = model;
    n.seed = uint32_t(npcs_.size()) * 2654435761u + 17u;
    pos.y = groundHeight(pos + Vec3{0, 30, 0});
    n.home = n.target = pos;
    n.radius = radius;
    n.body.teleport(pos, rnd(n.seed) * 2 * kPi);
    n.body.setArmed(false, true);
    n.wait = rnd(n.seed) * 4.0f;
    npcs_.push_back(n);
}

void Game::updateNpcs(float dt) {
    for (Npc& n : npcs_) {
        Vec3 toHero = flat(hero_.body.pos - n.body.body.pos);
        float dh = length(toHero);
        if (dh > kActiveRange) continue;
        Vec3 dir{};
        if (dh < kLook && hero_.downTimer < 0) {  // se para y la mira
            n.body.yaw += std::remainder(std::atan2(toHero.x, toHero.z) - n.body.yaw, 2 * kPi) * std::min(1.0f, dt * 4.0f);
            n.wait = std::max(n.wait, 1.0f);
        } else if (n.wait > 0) {
            n.wait -= dt;
            if (n.wait <= 0) {  // otro destino dentro de su zona
                float a = rnd(n.seed) * 2 * kPi, r = std::sqrt(rnd(n.seed)) * n.radius;
                n.target = n.home + Vec3{std::cos(a) * r, 0, std::sin(a) * r};
                n.walked = 0;
            }
        } else {
            Vec3 d = flat(n.target - n.body.body.pos);
            n.walked += dt;
            if (length(d) < 0.7f || n.walked > 12.0f) {  // llegó (o algo le corta el paso)
                n.wait = 2.0f + rnd(n.seed) * 5.0f;
            } else {
                dir = normalize(d) * kWalk;
            }
        }
        n.body.update(dt, dir, false, world_, n.body.yaw);
    }
}
