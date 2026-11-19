#include "dev/cable_bot.h"

#include <cmath>

namespace {

// Walk: ir hasta el punto. Talk: F (a quien esté delante). Ride: F en el andén de la línea y
// esperar a llegar. Wait: quieta unos fotogramas.
enum class Kind { Walk, Talk, Ride, Wait };
struct Stage {
    Kind kind;
    Vec3 at;
    int line = 0;
    bool fromA = true;
};

const Stage kStages[] = {
    {Kind::Walk, {6, 0, -1}},     {Kind::Walk, {11.7f, 0, -1}},  {Kind::Talk, {}},             // el armazém
    {Kind::Wait, {}},             {Kind::Walk, {8, 0, 12.2f}},   {Kind::Walk, {12, 0, 12.2f}},  // el botequim:
    {Kind::Walk, {15, 0, 12.2f}}, {Kind::Walk, {18.2f, 0, 12.2f}}, {Kind::Talk, {}},             // se entra hasta
    {Kind::Wait, {}},             {Kind::Walk, {15, 0, 12.2f}},  {Kind::Walk, {11, 0, 12.2f}},  // la barra y se sale
    {Kind::Walk, {8, 0, 4}},      {Kind::Walk, {11, 0, -6.5f}},  {Kind::Walk, {26, 0, -9.5f}},  // la Rua da Praia
    {Kind::Walk, {42, 0, -12}},   {Kind::Ride, {}, 0, true},                                   // a Urca
    {Kind::Ride, {}, 1, true},                                                                 // al Pão de Açúcar
    {Kind::Wait, {}},
};
constexpr int kStageCount = int(sizeof(kStages) / sizeof(kStages[0]));

}  // namespace

Controls CableBot::step(const Game& g, float& camYaw, const Controls& base) {
    Controls c;
    c.camForward = base.camForward;
    c.advance = base.advance;
    const Character& h = g.hero();
    Vec3 p = h.body.pos;
    camYaw += wrapAngle(h.yaw - camYaw) * 0.015f;  // la cámara, detrás
    if (stage_ >= kStageCount) {
        ++done_;
        return c;
    }
    const Stage& s = kStages[stage_];
    ++t_;
    auto walkTo = [&](Vec3 at) {
        Vec3 d = flat(at - p);
        if (length(d) < 0.4f) return true;
        c.move = normalize(d) * (length(d) < 1.5f ? 0.5f : 1.0f);
        return false;
    };
    bool next = false;
    switch (s.kind) {
        case Kind::Walk: next = walkTo(s.at) && t_ > 5; break;
        case Kind::Talk:
            c.interact = t_ == 10;
            next = t_ > 20;
            break;
        case Kind::Wait: next = t_ > 150; break;
        case Kind::Ride: {
            const CableLine& l = g.cableLines()[size_t(s.line)];
            if (!rode_ && !g.riding()) {
                if (walkTo(s.fromA ? l.stopA : l.stopB) && t_ > 5) c.interact = t_ % 20 == 0;
            }
            rode_ = rode_ || g.riding();
            next = rode_ && !g.riding() && g.rideFade() < 0.05f;
            break;
        }
    }
    if (next) {
        ++stage_;
        t_ = 0;
        rode_ = false;
    }
    return c;
}
