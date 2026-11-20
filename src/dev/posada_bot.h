#pragma once

#include "game/game.h"

// Bot de grabación de las tabernas de la bahía (--posada, con --nivel rio): desde la acera del
// Botequim entra (la pantalla de carga), cruza la sala junto a la chimenea, sube la escalera a
// la galería, la recorre y se mete en el cuarto abierto. La cámara va detrás, girando despacio.
class PosadaBot {
public:
    Controls step(const Game& g, float& camYaw, const Controls& base);
    bool done() const { return done_ > 120; }

private:
    int stage_ = 0, t_ = 0, done_ = 0;
};
