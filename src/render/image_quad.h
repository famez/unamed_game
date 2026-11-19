#pragma once

// Una imagen RGBA en la GPU dibujada en un rectángulo de la interfaz (el mapa del nivel). Se
// sube una vez y se dibuja con filtro lineal y mipmaps (se ve bien alejada y de cerca).

#include "core/gl.h"
#include "core/vmath.h"

#include <cstdint>

class ImageQuad {
public:
    bool init();
    void shutdown();
    void upload(int w, int h, const uint32_t* rgba);
    bool ready() const { return width_ > 0; }
    // El rectángulo (x0, y0)-(x1, y1) en las coordenadas de `pixels` (las de la interfaz), con la
    // imagen entera; `alpha` la funde. El recorte lo pone quien llama (glScissor).
    void draw(const Mat4& pixels, float x0, float y0, float x1, float y1, float alpha) const;

private:
    GLuint program_ = 0, vao_ = 0, vbo_ = 0, tex_ = 0;
    int width_ = 0, height_ = 0;
};
