#pragma once

#include "game/game.h"

// Bot de grabación de Los Andamios (--escalada): hace la subida entera, tramo a tramo (la
// escalera, las losas, el pozo de paredes, la cornisa y la escalera que cuelga). Si se cae,
// sigue desde el tramo en el que va. La cámara va detrás, girando despacio.
class ClimbBot {
public:
    Controls step(const Game& g, float& camYaw, const Controls& base);
    bool done() const { return done_ > 180; }  // ya arriba, tres segundos mirando

private:
    void startStage(int s);
    int stage_ = 0, t_ = 0, done_ = 0, lastPart_ = 0;
    bool jumped_ = false, doubled_ = false, wallJumped_ = false, started_ = false;
    Vec3 lastPos_;
    float startY_ = 0;
};
