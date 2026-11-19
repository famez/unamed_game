#pragma once

// Mapa de un nivel: la zona vista desde arriba, pintada una vez al abrirlo (Game::buildLevelMap).
// La imagen va orientada como se ve el mundo: arriba, +z; a la derecha, −x (la derecha de quien
// mira hacia +z). Lo que cambia (la protagonista, el objetivo, las cápsulas abiertas...) se
// dibuja encima, en la interfaz (ui/map_screen.cpp).

#include "core/vmath.h"

#include <cstdint>
#include <vector>

struct LevelMap {
    int w = 0, h = 0;
    float xLeft = 0, zTop = 0;  // el mundo en la esquina de arriba a la izquierda de la imagen
    float cell = 1;             // metros por píxel
    std::vector<uint32_t> pixels;  // RGBA por filas, de arriba abajo (alfa 0: el vacío)
    uint32_t serial = 0;           // la zona cargada para la que se pintó (Game::levelSerial)

    // Del mundo a la imagen (en píxeles, desde la esquina de arriba a la izquierda) y al revés.
    float u(Vec3 p) const { return (xLeft - p.x) / cell; }
    float v(Vec3 p) const { return (zTop - p.z) / cell; }
    Vec3 world(float u, float v) const { return {xLeft - u * cell, 0, zTop - v * cell}; }
    bool empty() const { return pixels.empty(); }
};
