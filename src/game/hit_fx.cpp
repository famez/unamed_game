// Efectos de golpe: tajo, destello, esquirlas o tinta, sacudida del enemigo, tirón de cámara y
// líneas de impacto en los remates (ver HitFx en game.h). Aquí se crean y se mueven; se
// dibujan en game_draw.cpp.

#include "game/game.h"

#include <algorithm>
#include <cstring>
#include <string>

bool parseHitFx(const char* list, unsigned& mask) {
    struct Name {
        const char* name;
        unsigned bit;
    };
    const Name names[] = {{"tajo", kFxSlash},     {"destello", kFxFlash}, {"materia", kFxMatter}, {"sacudida", kFxRecoil},
                          {"camara", kFxCamera}, {"remate", kFxFinisher}, {"todos", kFxAll},    {"ninguno", 0}};
    mask = 0;
    std::string s(list);
    size_t at = 0;
    while (at <= s.size()) {
        size_t end = s.find(',', at);
        if (end == std::string::npos) end = s.size();
        std::string word = s.substr(at, end - at);
        bool known = false;
        for (const Name& n : names)
            if (word == n.name) mask |= n.bit, known = true;
        if (!known) return false;
        at = end + 1;
    }
    return true;
}

namespace {

// Hacia dónde corre el tajo de cada golpe (en el espacio del personaje: x = su izquierda,
// y = arriba, z = delante) y si es un golpe de punta (estocada, embestida, puñetazo).
struct SlashShape {
    Vec3 along;
    bool stab;
};

SlashShape slashShape(Move m) {
    switch (m) {
        case Move::Slash1: case Move::Air2: return {{1, -0.15f, 0}, false};  // de derecha a izquierda
        case Move::Slash2: return {{-1, 0.1f, 0}, false};                     // revés
        case Move::Slash3: case Move::Counter: case Move::AirFinisher: case Move::Sweep: return {{1, 0.05f, 0}, false};
        case Move::Finisher: case Move::Air1: case Move::Charged: case Move::Smash: case Move::Quake: case Move::Plunge:
            return {{0.2f, -1, 0}, false};  // de arriba abajo
        case Move::Launcher: case Move::Uppercut: return {{-0.15f, 1, 0}, false};
        default: return {{0, 0, 1}, true};  // Thrust, Rush, Punch, PunchL
    }
}

}  // namespace

void Game::meleeHitFx(const Enemy& e, Move m, Vec3 dir) {
    const WeaponStats& w = inv_.weaponStats();
    Vec3 fwd = hero_.forward(), left{std::cos(hero_.yaw), 0, -std::sin(hero_.yaw)};
    Vec3 c = e.center();
    bool fin = isFinisher(m);
    if (hitFx_ & kFxSlash) {
        SlashShape sh = slashShape(m);
        Vec3 along = normalize(left * sh.along.x + Vec3{0, sh.along.y, 0} + fwd * sh.along.z);
        // Un poco de variedad: el plano del tajo se ladea al azar.
        float tilt = randRange(-0.3f, 0.3f);
        Vec3 side = normalize(cross(fwd, along));
        if (!sh.stab) along = normalize(along * std::cos(tilt) + side * std::sin(tilt));
        Vec3 bend = -fwd;  // la media luna se abre hacia quien golpea
        bend = normalize(bend - along * dot(bend, along));
        if (sh.stab) bend = Vec3{0, 1, 0};
        float size = (fin ? 1.6f : 1.1f) * std::max(0.8f, e.size * 0.9f) * (sh.stab ? 0.8f : 1.0f);
        uint32_t col = w.trail ? w.trail : rgba(255, 230, 160);
        slashes_.push_back({c - dir * 0.15f, along, sh.stab ? bend * 0.08f : bend, 0, fin ? 0.26f : 0.2f, size, col});
        if (fin && !sh.stab) {  // en los remates, un segundo tajo cruzado
            Vec3 cross2 = normalize(along * 0.3f + side * 0.95f);
            slashes_.push_back({c - dir * 0.1f, cross2, normalize(bend - cross2 * dot(bend, cross2)), -0.04f, 0.24f, size * 0.85f,
                                col});
        }
    }
    if ((hitFx_ & kFxFinisher) && fin) {
        flashes_.push_back({c, dir, 0, 0.24f, 1.4f * std::max(1.0f, e.size), randRange(0, kPi), rgba(255, 250, 235), true});
        camKickVel_ += dir * 2.0f * float((hitFx_ & kFxCamera) != 0);
    }
}

void Game::impactFx(Enemy& e, Vec3 point, Vec3 dir, float strength) {
    const EnemyDef& d = e.def();
    if (hitFx_ & kFxFlash) {
        uint32_t col = d.shadow ? rgba(225, 205, 255) : rgba(255, 245, 215);
        flashes_.push_back({point, dir, 0, 0.11f, 0.55f + 0.25f * strength, randRange(0, kPi), col, false});
    }
    if (hitFx_ & kFxMatter) {
        int n = isSwarm(e.kind) ? 4 : int(5.0f + 4.0f * strength);
        float ground = groundHeight(point + Vec3{0, 0.5f, 0});
        Vec3 side{-dir.z, 0, dir.x};
        for (int i = 0; i < n && debris_.size() < 220; ++i) {
            Vec3 v = dir * randRange(2.0f, 5.0f) + side * randRange(-2.5f, 2.5f) + Vec3{0, randRange(1.5f, 4.5f), 0};
            if (d.shadow) {  // tinta: gotas espesas que caen y manchan el suelo
                debris_.push_back({point + side * randRange(-0.1f, 0.1f), v * 0.8f, 0, 1.5f, randRange(0.03f, 0.065f), 0, ground,
                                   random01() < 0.2f ? rgba(70, 30, 110) : rgba(16, 9, 26), true});
            } else {  // cristal: esquirlas que giran, rebotan y se apagan
                uint32_t col = random01() < 0.5f ? rgba(255, 244, 205) : rgba(205, 235, 255);
                debris_.push_back({point, v, 0, randRange(0.6f, 1.0f), randRange(0.6f, 1.2f), randRange(-20.0f, 20.0f), ground,
                                   col, false});
            }
        }
    }
    if (hitFx_ & kFxRecoil) hitReaction(e, dir, strength);
    if (hitFx_ & kFxCamera) camKickVel_ += dir * (0.9f + 0.6f * strength);
}

void Game::updateHitFx(float dt) {
    for (SlashMark& s : slashes_) s.age += dt;
    slashes_.erase(std::remove_if(slashes_.begin(), slashes_.end(), [](const SlashMark& s) { return s.age >= s.life; }),
                   slashes_.end());
    for (ImpactFlash& f : flashes_) f.age += dt;
    flashes_.erase(std::remove_if(flashes_.begin(), flashes_.end(), [](const ImpactFlash& f) { return f.age >= f.life; }),
                   flashes_.end());
    for (Debris& b : debris_) {
        b.age += dt;
        b.vel.y -= (b.ink ? 16.0f : 12.0f) * dt;
        b.pos += b.vel * dt;
        if (b.pos.y <= b.ground + 0.02f && b.vel.y < 0) {
            if (b.ink) {  // la gota se aplasta en una mancha
                b.age = b.life;
                if (splats_.size() >= 48) splats_.erase(splats_.begin());
                Vec3 v = flat(b.vel);
                splats_.push_back({{b.pos.x, b.ground + 0.012f, b.pos.z}, 0, randRange(2.5f, 4.0f), b.size * randRange(2.2f, 3.4f),
                                   std::atan2(v.x, v.z), 1.0f + std::min(1.2f, length(v) * 0.25f)});
            } else {  // la esquirla rebota sin fuerza y se queda brillando un momento
                b.pos.y = b.ground + 0.02f;
                b.vel = Vec3{b.vel.x * 0.4f, -b.vel.y * 0.3f, b.vel.z * 0.4f};
                b.spin *= 0.5f;
            }
        }
    }
    debris_.erase(std::remove_if(debris_.begin(), debris_.end(), [](const Debris& b) { return b.age >= b.life; }), debris_.end());
    for (InkSplat& s : splats_) s.age += dt;
    splats_.erase(std::remove_if(splats_.begin(), splats_.end(), [](const InkSplat& s) { return s.age >= s.life; }), splats_.end());
    // La cámara: un muelle con poco amortiguamiento (tirón, se pasa un pelo y vuelve).
    const float omega = 28.0f, zeta = 0.45f;
    camKickVel_ = (camKickVel_ - camKick_ * (omega * omega * dt)) * (1.0f / (1.0f + 2.0f * zeta * omega * dt + omega * omega * dt * dt));
    camKick_ += camKickVel_ * dt;
}
