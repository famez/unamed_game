#pragma once

// Renderizador "modo debug": acumula triángulos, líneas y puntos en la CPU durante el
// frame y los envía a la GPU en un único buffer y tres draw calls. Sin iluminación ni
// texturas: el shader solo transforma el vértice y pinta su color.

#include "gl.h"
#include "vmath.h"

#include <cstdint>
#include <vector>

// Color empaquetado RGBA8 (en memoria: r, g, b, a en little-endian).
constexpr uint32_t rgba(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255) {
    return uint32_t(r) | uint32_t(g) << 8 | uint32_t(b) << 16 | uint32_t(a) << 24;
}

struct DebugVertex {
    float x, y, z;
    uint32_t color;
};

class DebugDraw {
public:
    bool init();
    void shutdown();

    void line(Vec3 a, Vec3 b, uint32_t color);
    void point(Vec3 p, uint32_t color);
    // Caja alineada con los ejes de `transform` (12 aristas).
    void box(const Mat4& transform, Vec3 center, Vec3 halfExtents, uint32_t color);
    void circleXZ(Vec3 center, float radius, int segments, uint32_t color);
    // Relleno (con alfa si el blending está activo). Esquinas en orden.
    void triangle(Vec3 a, Vec3 b, Vec3 c, uint32_t color);
    void quad(Vec3 a, Vec3 b, Vec3 c, Vec3 d, uint32_t color);

    // Dibuja todo lo acumulado y vacía las listas.
    void flush(const Mat4& viewProj, float pointSize);

    size_t vertexCount() const { return tris_.size() + lines_.size() + points_.size(); }

private:
    std::vector<DebugVertex> tris_;
    std::vector<DebugVertex> lines_;
    std::vector<DebugVertex> points_;
    GLuint vao_ = 0, vbo_ = 0, program_ = 0;
    GLint uViewProj_ = -1, uPointSize_ = -1;
    size_t gpuCapacity_ = 0;  // en vértices
};
