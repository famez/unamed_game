#include "dev/climb_bot.h"

#include <cmath>

namespace {

// Walk: ir hasta `a`. Ladder: empujar hacia `a` hasta pisar a `goal` m. Hop: correr hacia `b`
// y saltar al pasar por `a` (con doble salto si `twice`). Wall: de pared en pared, empezando
// hacia `a`, hasta pisar a `goal` m. Hang: hacia `a` y saltar al pasar por `b` (a una escalera
// que cuelga); luego, arriba.
enum class Kind { Walk, Ladder, Hop, Wall, Hang };
struct Stage {
    Kind kind;
    Vec3 a, b;
    bool twice = false;
    float goal = 0;
};

const Stage kStages[] = {
    {Kind::Walk, {0, 0, 3}, {}},                               // 0: al pie de la escalera
    {Kind::Ladder, {0, 0, 1}, {}, false, 5.0f},
    {Kind::Walk, {0, 0, 7.5f}, {}},                            // 2: los saltos
    {Kind::Hop, {0, 0, 9.6f}, {0, 0, 13.5f}},
    {Kind::Hop, {0, 0, 14.6f}, {0, 0, 18.0f}},
    {Kind::Walk, {0, 0, 17.0f}, {}},
    {Kind::Hop, {0, 0, 18.6f}, {3.0f, 0, 22.5f}, true},
    {Kind::Walk, {5.0f, 0, 21.0f}, {}},                        // 7: el pozo
    {Kind::Walk, {8.2f, 0, 21.0f}, {}},
    {Kind::Walk, {8.2f, 0, 23.0f}, {}},
    {Kind::Wall, {1, 0, 0}, {}, true, 15.2f},
    {Kind::Walk, {12.0f, 0, 24.5f}, {}},                       // 11: las cornisas
    {Kind::Hop, {12.0f, 0, 25.8f}, {12.0f, 0, 28.6f}},
    {Kind::Hop, {12.0f, 0, 29.4f}, {12.0f, 0, 31.6f}},
    {Kind::Hop, {11.3f, 0, 31.7f}, {9.0f, 0, 32.0f}},
    {Kind::Walk, {9.0f, 0, 34.0f}, {}},
    {Kind::Wall, {1, 0, 0}, {}, true, 21.2f},
    {Kind::Walk, {4.5f, 0, 34.0f}, {}},                        // 17: la escalera que cuelga
    {Kind::Hang, {0, 0, 1}, {0, 0, 35.6f}, false, 27.0f},
    {Kind::Walk, {4.5f, 0, 39.2f}, {}},                        // 19: junto a la linterna
};
constexpr int kStageCount = int(sizeof(kStages) / sizeof(kStages[0]));
const int kPartStage[] = {0, 2, 7, 11, 17};  // por dónde se sigue si cae en cada tramo

}  // namespace

void ClimbBot::startStage(int s) {
    stage_ = s;
    t_ = 0;
    jumped_ = doubled_ = wallJumped_ = started_ = false;
}

Controls ClimbBot::step(const Game& g, float& camYaw, const Controls& base) {
    Controls c;
    c.camForward = base.camForward;
    c.advance = base.advance;
    const Character& h = g.hero();
    Vec3 p = h.body.pos;
    camYaw += wrapAngle(0.0f - camYaw) * 0.02f;  // la cámara, detrás (la subida va hacia +z)
    // Se ha caído (vuelve al último tramo de golpe): sigue desde ahí.
    if (t_ > 0 && length(p - lastPos_) > 3.0f) {
        int part = std::min(g.partIndex(), 4);
        startStage(kPartStage[part]);
    }
    lastPos_ = p;
    if (stage_ >= kStageCount) {
        ++done_;
        return c;
    }
    const Stage& s = kStages[stage_];
    ++t_;
    auto toward = [&](Vec3 at) { return length(flat(at - p)) > 0.35f ? normalize(flat(at - p)) : Vec3{}; };
    bool next = false;
    switch (s.kind) {
        case Kind::Walk:
            c.move = toward(s.a);
            next = length(c.move) == 0 && h.body.grounded;
            break;
        case Kind::Ladder:
            c.move = s.a;
            next = h.body.grounded && p.y > s.goal - 0.1f;
            break;
        case Kind::Hop: {
            Vec3 dir = normalize(flat(s.b - s.a));
            c.move = toward(s.b);
            if (!jumped_ && dot(flat(p - s.a), dir) > 0) c.jump = jumped_ = true;
            else if (s.twice && jumped_ && !doubled_ && h.body.vel.y < 0.5f && !h.body.grounded) c.jump = doubled_ = true;
            started_ = started_ || !h.body.grounded;
            next = jumped_ && started_ && h.body.grounded && !h.climbing() && t_ > 20;
            break;
        }
        case Kind::Wall: {
            if (t_ == 1) startY_ = p.y;
            bool high = h.body.grounded && started_ && p.y > startY_ + 0.5f && !h.climbing();  // encaramada a medio camino
            c.move = started_ && !high ? h.forward() : s.a;
            if (!started_ && t_ > 20) c.jump = started_ = true;
            if (high && dot(h.forward(), s.a) > 0.95f) c.jump = wallJumped_ = true, doubled_ = false;
            else if (h.climb() == Character::Climb::Wall && h.climbTime() > 0.12f) c.jump = wallJumped_ = true, doubled_ = false;
            else if (s.twice && wallJumped_ && !doubled_ && !h.climbing() && !h.body.grounded && h.body.vel.y < 1.0f)
                c.jump = doubled_ = true;
            next = h.body.grounded && p.y > s.goal - 0.1f;
            break;
        }
        case Kind::Hang:
            c.move = h.body.grounded && p.y > s.goal - 0.5f ? Vec3{} : s.a;
            if (!jumped_ && p.z > s.b.z) c.jump = jumped_ = true;
            next = h.body.grounded && p.y > s.goal - 0.1f;
            break;
    }
    if (next) startStage(stage_ + 1);
    return c;
}
