#pragma once

#include "game/game.h"

// Bot de grabación de la Ciudad de la Bahía (--teleferico): compra en el armazém, descansa en
// el botequim, baja a la estación de la caleta y sube en el teleférico al Morro da Urca y
// de allí al Pão de Açúcar. La cámara va detrás, girando despacio.
class CableBot {
public:
    Controls step(const Game& g, float& camYaw, const Controls& base);
    bool done() const { return done_ > 150; }
    // Lleva la tienda abierta tanto rato (el menú lo cierra main).
    bool shopShown(int frames) const { return shopFrames_ > frames; }
    void shopOpen() { ++shopFrames_; }

private:
    int stage_ = 0, t_ = 0, done_ = 0, shopFrames_ = 0;
    bool rode_ = false;
};
