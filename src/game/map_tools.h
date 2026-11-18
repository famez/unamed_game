#pragma once

// Herramientas para generar mapas grandes: generador aleatorio propio (cada mapa sale siempre
// igual), ruido suave para el relieve y distancias a caminos.

#include "core/vmath.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace maptools {

// Generador propio (no el global): cada mapa sale siempre igual.
struct Rng {
    uint32_t s;
    float next() {
        s = s * 1664525u + 1013904223u;
        return float(s >> 8) / 16777216.0f;
    }
    float range(float a, float b) { return a + (b - a) * next(); }
    int pick(int n) { return std::min(n - 1, int(next() * float(n))); }
};

inline float hash2(int x, int z, uint32_t seed) {
    uint32_t h = uint32_t(x) * 374761393u + uint32_t(z) * 668265263u + seed * 2246822519u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return float((h ^ (h >> 16)) & 0xFFFFFF) / 16777216.0f;
}

// Ruido de valor suave en [-1, 1].
inline float noise(float x, float z, uint32_t seed) {
    int ix = int(std::floor(x)), iz = int(std::floor(z));
    float fx = x - float(ix), fz = z - float(iz);
    fx = fx * fx * (3 - 2 * fx), fz = fz * fz * (3 - 2 * fz);
    float a = hash2(ix, iz, seed), b = hash2(ix + 1, iz, seed), c = hash2(ix, iz + 1, seed), d = hash2(ix + 1, iz + 1, seed);
    return (a + (b - a) * fx + (c - a) * fz + (a - b - c + d) * fx * fz) * 2 - 1;
}

inline float fbm(float x, float z, uint32_t seed) {
    return noise(x, z, seed) * 0.6f + noise(x * 2.1f, z * 2.1f, seed + 1) * 0.28f + noise(x * 4.3f, z * 4.3f, seed + 2) * 0.12f;
}

// Distancia de un punto (XZ) a una polilínea, y el parámetro del punto más cercano.
inline float distToPath(Vec3 p, const std::vector<Vec3>& path, Vec3* closest = nullptr) {
    float best = 1e9f;
    for (size_t i = 0; i + 1 < path.size(); ++i) {
        Vec3 a = path[i], b = path[i + 1], ab = flat(b - a);
        float t = clampf(dot(flat(p - a), ab) / std::max(1e-4f, dot(ab, ab)), 0, 1);
        Vec3 q = a + ab * t;
        float d = length(flat(p - q));
        if (d < best) {
            best = d;
            if (closest) *closest = q;
        }
    }
    return best;
}

// Distancias con signo en el plano XZ (negativas dentro), para tallar zonas en el relieve.
inline float sdBox(Vec3 p, Vec3 c, float hx, float hz, float round = 0) {
    float qx = std::fabs(p.x - c.x) - hx + round, qz = std::fabs(p.z - c.z) - hz + round;
    float ox = std::max(qx, 0.0f), oz = std::max(qz, 0.0f);
    return std::sqrt(ox * ox + oz * oz) + std::min(std::max(qx, qz), 0.0f) - round;
}
inline float sdCircle(Vec3 p, Vec3 c, float r) { return length(flat(p - c)) - r; }

inline float smoothFalloff(float d, float r) { return d >= r ? 0.0f : 1.0f - smoothstep01(d / r); }

}  // namespace maptools
