#pragma once

// Física de cuerpos rígidos con Jolt Physics: ragdolls de los personajes derribados y
// objetos sueltos (cajas). Convive con physics.h, que sigue moviendo a los personajes
// vivos (controlador propio con AABB): estos entran en Jolt como cápsulas cinemáticas
// que empujan lo dinámico pero no se dejan empujar.
//
// La interfaz solo usa los tipos del juego (Vec3, Mat4): ninguna cabecera de Jolt sale de
// physics_jolt.cpp.

#include "physics.h"
#include "skeleton.h"
#include "vmath.h"

#include <cstddef>
#include <memory>

class RigidWorld {
public:
    RigidWorld();
    ~RigidWorld();
    RigidWorld(RigidWorld&&) noexcept;
    RigidWorld& operator=(RigidWorld&&) noexcept;

    // Crea el mundo estático (suelo + cajas del nivel). Llamar una vez.
    void build(const World& world);
    void step(float dt);

    // Ragdoll en la pose actual del esqueleto (matrices mundo) y con velocidad inicial.
    // Devuelve un identificador o -1 si no se pudo crear.
    int createRagdoll(const Mat4 joints[JointCount], Vec3 velocity);
    void destroyRagdoll(int id);
    // Matrices mundo de las articulaciones según los cuerpos del ragdoll.
    void ragdollJoints(int id, Mat4 out[JointCount]) const;
    Vec3 ragdollPelvis(int id) const;

    // Cajas dinámicas sueltas por el nivel.
    void addCrate(Vec3 center, Vec3 half, float mass);
    size_t crateCount() const;
    Mat4 crateTransform(size_t i) const;
    Vec3 crateHalf(size_t i) const;

    // Cápsula cinemática (personaje vivo). `feet` = posición de los pies.
    int addProxy(float radius, float height);
    void moveProxy(int id, Vec3 feet, float dt);
    void setProxyEnabled(int id, bool enabled);

    // Rayo contra lo dinámico (cajas y ragdolls). t ∈ [0,1] sobre el segmento.
    bool raycast(Vec3 from, Vec3 to, float& t, Vec3& normal) const;
    // Impulso (N·s) en el primer cuerpo dinámico que cruza el segmento.
    bool impulseAlong(Vec3 from, Vec3 to, float impulse);
    // Cambio de velocidad radial (m/s en el centro, cae hasta 0 en `radius`) + hacia arriba.
    void explosion(Vec3 center, float radius, float speed, float lift);
    // Cambio de velocidad a todo lo dinámico dentro de una esfera (golpes cuerpo a cuerpo).
    void push(Vec3 center, float radius, Vec3 velocity);

    size_t dynamicBodyCount() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
