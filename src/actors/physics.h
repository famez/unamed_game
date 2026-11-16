#pragma once

// Física mínima: el mundo son cajas alineadas a los ejes (AABB) más un suelo infinito
// en y = 0 o, en los mapas grandes, un terreno con relieve (mapa de alturas). Los personajes
// son cajas verticales que se mueven y se resuelven eje por eje (primero X, luego Z, luego
// Y): simple, estable y suficiente para plataformas.

#include "core/vmath.h"

#include <memory>
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

// Terreno: alturas en una rejilla regular (interpoladas entre los puntos). Fuera de la
// rejilla sigue la altura del borde.
struct Terrain {
    float x0 = 0, z0 = 0;  // esquina de la rejilla
    float cell = 2;        // distancia entre puntos (m)
    int nx = 0, nz = 0;    // puntos en cada eje
    std::vector<float> heights;  // nx * nz, por filas de z

    float& at(int ix, int iz) { return heights[size_t(iz) * size_t(nx) + size_t(ix)]; }
    float at(int ix, int iz) const { return heights[size_t(iz) * size_t(nx) + size_t(ix)]; }
    float heightAt(float x, float z) const;
    Vec3 normalAt(float x, float z) const;
};

// Rejilla de cajas por celdas en el plano XZ: con miles de piezas, cada consulta solo mira
// las de su zona.
struct SolidGrid {
    float x0 = 0, z0 = 0, cell = 8;
    int nx = 0, nz = 0;
    size_t count = 0;  // cajas con las que se construyó (si cambian, no se usa)
    std::vector<std::vector<int>> cells;
};

struct World {
    std::vector<AABB> solids;
    bool ground = true;  // suelo infinito en y = 0 (sin él se puede caer al vacío)
    std::shared_ptr<const Terrain> terrain;  // relieve (sustituye al suelo plano)
    SolidGrid grid;

    float groundAt(float x, float z) const { return terrain ? terrain->heightAt(x, z) : 0.0f; }
    // Prepara la rejilla (tras cambiar `solids`; con pocas cajas no hace falta).
    void buildGrid();
    // Índices de las cajas que pueden tocar la zona [lo, hi] en XZ (todas, sin rejilla).
    void query(Vec3 lo, Vec3 hi, std::vector<int>& out) const;
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
