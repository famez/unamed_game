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

// Otros esqueletos (los de los enemigos) usan la misma estructura con sus propias
// articulaciones; ninguno pasa de kMaxJoints.
constexpr int kMaxJoints = 20;

struct Pose {
    Vec3 rotation[kMaxJoints];  // Euler locales (radianes): se aplica Y * X * Z
    Vec3 rootOffset;            // desplazamiento extra de la raíz (balanceo vertical)
};

// Mezcla de poses (ángulos por el camino corto): para pasar de un estado a otro sin saltos.
inline Pose lerpPose(const Pose& a, const Pose& b, float t, int count = kMaxJoints) {
    Pose r = b;
    for (int i = 0; i < count; ++i)
        r.rotation[i] = {lerpAngle(a.rotation[i].x, b.rotation[i].x, t), lerpAngle(a.rotation[i].y, b.rotation[i].y, t),
                         lerpAngle(a.rotation[i].z, b.rotation[i].z, t)};
    r.rootOffset = a.rootOffset + (b.rootOffset - a.rootOffset) * t;
    return r;
}

// Calcula la matriz mundo de cada joint de un esqueleto cualquiera. `model` lo coloca en
// el mundo.
void computeWorldMatrices(const JointDef* rig, int count, const Pose& pose, const Mat4& model, Mat4* out);
// Esqueleto humanoide (el del protagonista).
inline void computeWorldMatrices(const Pose& pose, const Mat4& model, Mat4 out[JointCount]) {
    computeWorldMatrices(kSkeleton, JointCount, pose, model, out);
}
