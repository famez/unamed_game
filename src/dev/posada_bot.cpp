#include "dev/posada_bot.h"

#include <cmath>

namespace {

// Walk: ir hasta el punto. Enter: F en la puerta. Wait: quieta unos fotogramas.
enum class Kind { Walk, Enter, Wait };
struct Stage {
    Kind kind;
    Vec3 at;
};

const Stage kStages[] = {
    {Kind::Wait, {}},          {Kind::Walk, {12.2f, 0, 12.2f}}, {Kind::Enter, {}},  // en la acera, F
    {Kind::Wait, {}},          {Kind::Walk, {1.8f, 0, 2.6f}},   {Kind::Walk, {2.0f, 0, -1.6f}},  // junto al fuego
    {Kind::Walk, {-1.0f, 0, -1.9f}}, {Kind::Walk, {-3.9f, 0, -1.5f}}, {Kind::Walk, {-3.9f, 0, 2.7f}},  // entre las mesas
    {Kind::Walk, {-4.9f, 0, 3.0f}},                                                                    // al pie de la escalera
    {Kind::Walk, {-4.9f, 0, -2.7f}}, {Kind::Walk, {-4.4f, 0, -3.9f}}, {Kind::Walk, {3.6f, 0, -3.9f}},  // arriba, la galería
    {Kind::Walk, {3.6f, 0, -6.2f}},  {Kind::Wait, {}},                                                // el cuarto
};
constexpr int kStageCount = int(sizeof(kStages) / sizeof(kStages[0]));

}  // namespace

Controls PosadaBot::step(const Game& g, float& camYaw, const Controls& base) {
    Controls c;
    c.camForward = base.camForward;
    c.advance = base.advance;
    const Character& h = g.hero();
    Vec3 p = h.body.pos;
    camYaw += wrapAngle(h.yaw - camYaw) * 0.02f;  // la cámara, detrás
    if (stage_ >= kStageCount) {
        ++done_;
        return c;
    }
    const Stage& s = kStages[stage_];
    ++t_;
    bool next = false;
    switch (s.kind) {
        case Kind::Walk: {
            Vec3 d = flat(s.at - p);
            if (length(d) < 0.4f) {
                next = t_ > 5;
                break;
            }
            c.move = normalize(d) * (length(d) < 1.5f ? 0.45f : 0.55f);  // andando, para que se vea la sala
            next = t_ > 60 * 20;  // atascada: sigue con lo siguiente
            break;
        }
        case Kind::Enter:
            c.interact = t_ == 10;
            next = t_ > 20;
            break;
        case Kind::Wait: next = t_ > 150; break;
    }
    if (next) {
        ++stage_;
        t_ = 0;
    }
    return c;
}
