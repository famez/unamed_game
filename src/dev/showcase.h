#pragma once

#include <vector>

#include "game/game.h"

// Guion de grabación (--vitrina): en el patio de combate, el protagonista enseña por actos
// los ataques, técnicas y formas de la luz y de la sombra, el viaje por la luz, romper
// farolas, los interruptores y fundirse con la sombra. Cada acto lleva su rótulo.
class Showcase {
public:
    // Controles de este paso (60 Hz). `camYaw` lo puede girar el guion al empezar un acto.
    Controls step(Game& g, float& camYaw, const Controls& base);
    bool done() const;
    // El acto acaba de colocar al protagonista: la cámara debe saltar con él.
    bool takeCut() {
        bool c = cut_;
        cut_ = false;
        return c;
    }
    const char* title() const;
    const char* subtitle() const;
    float captionAlpha() const;  // aparece y se desvanece al principio de cada acto
    // Solo esos actos (lista de índices, 0 = el primero); vacío = todos.
    void only(std::vector<int> acts) { only_ = std::move(acts); }

private:
    int act_ = -1, t_ = 0;
    bool cut_ = false;
    Vec3 home_;            // sitio despejado del acto
    Vec3 dir_;             // hacia dónde empujar
    int lampA_ = -1, lampB_ = -1, switch_ = -1;
    int guard_ = 0;  // pasos que le quedan con la guardia levantada
    std::vector<int> only_;
};
