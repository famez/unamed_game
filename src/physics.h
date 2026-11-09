#pragma once

// Física mínima: el mundo son cajas alineadas a los ejes (AABB) más un suelo infinito
// en y = 0. Los personajes son cajas verticales que se mueven y se resuelven eje por eje
// (primero X, luego Z, luego Y): simple, estable y suficiente para plataformas.

#include "vmath.h"

#include <vector>

constexpr float kGravity = 20.0f;  // m/s²: más que la real (9,8) para que el salto no flote

struct AABB {
    Vec3 min, max;

    Vec3 center() const { return (min + max) * 0.5f; }
    Vec3 halfExtents() const { return (max - min) * 0.5f; }
};

inline AABB boxFromCenter(Vec3 c, Vec3 half) { return {c - half, c + half}; }

inline bool overlaps(const AABB& a, const AABB& b) {
    return a.min.x < b.max.x && a.max.x > b.min.x && a.min.y < b.max.y && a.max.y > b.min.y &&
           a.min.z < b.max.z && a.max.z > b.min.z;
}

struct World {
    std::vector<AABB> solids;
};

// Cuerpo vertical con los pies en `pos`.
struct Body {
    Vec3 pos;
    Vec3 vel;
    float radius = 0.3f;
    float height = 1.8f;
    bool grounded = false;

    AABB bounds() const {
        return {{pos.x - radius, pos.y, pos.z - radius},
                {pos.x + radius, pos.y + height, pos.z + radius}};
    }
};

// Aplica gravedad, integra la velocidad y resuelve colisiones contra el mundo.
// `stepHeight`: altura de bordillo que se sube andando sin saltar (0 = ninguna).
void moveBody(Body& body, const World& world, float dt, float stepHeight);

struct RayHit {
    float t = 0;  // fracción del segmento [0, 1]
    Vec3 normal;
};

// Segmento p0→p1 contra una caja (método de los "slabs").
bool segmentVsAABB(Vec3 p0, Vec3 p1, const AABB& box, RayHit& hit);
// Contra todo el mundo (cajas + suelo); devuelve el impacto más cercano.
bool segmentVsWorld(Vec3 p0, Vec3 p1, const World& world, RayHit& hit);
