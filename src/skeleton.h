#pragma once

// Esqueleto humanoide: jerarquía de articulaciones (joints) con un offset fijo respecto
// a su padre ("bind pose", en metros) y una rotación local que cambia con la animación.
// La posición en el mundo se obtiene por cinemática directa (forward kinematics):
//     world[i] = world[parent[i]] * translate(offset[i]) * rotación[i]
//
// Cada joint lleva además una caja en su espacio local que representa el trozo de
// cuerpo que mueve (muslo, antebrazo...). Es "skinning rígido": cada vértice pertenece
// a un único hueso, igual que muchos juegos de PS2/PC de principios de los 2000.

#include "vmath.h"

enum Joint : int {
    Pelvis, Spine, Chest, Neck, Head,
    ShoulderL, ElbowL, WristL,
    ShoulderR, ElbowR, WristR,
    HipL, KneeL, AnkleL, ToeL,
    HipR, KneeR, AnkleR, ToeR,
    JointCount
};

struct JointDef {
    const char* name;
    int parent;       // -1 = raíz. Siempre menor que el índice propio (orden topológico).
    Vec3 offset;      // posición respecto al padre en la pose de reposo
    Vec3 boxCenter;   // caja del segmento corporal, en espacio local del joint
    Vec3 boxHalf;     // semiejes; (0,0,0) = sin caja
};

extern const JointDef kSkeleton[JointCount];

struct Pose {
    Vec3 rotation[JointCount];  // Euler locales (radianes): se aplica Y * X * Z
    Vec3 rootOffset;            // desplazamiento extra de la pelvis (balanceo vertical)
};

// Calcula la matriz mundo de cada joint. `model` coloca al personaje en el mundo.
void computeWorldMatrices(const Pose& pose, const Mat4& model, Mat4 out[JointCount]);
