#pragma once

// Enemigos: cada especie tiene su propio esqueleto (rig), sus formas de cuerpo (elipsoides
// pegados a las articulaciones), sus estadísticas y su animación procedural. La IA (qué
// decide hacer en cada estado) está en game.cpp, porque necesita al jugador y al mundo.

#include "physics.h"
#include "skeleton.h"
#include "vmath.h"

#include <cstdint>

enum class EnemyKind { Imp, Shroom, Moth, Count };

// Ciclo de combate: aparece, persigue, avisa (preparación), ataca y se recupera. Un golpe
// le hace tambalearse (interrumpe la preparación).
enum class AiState { Spawning, Idle, Chase, Windup, Attack, Recover, Stagger, Dying };

const char* aiStateName(AiState s);

// Trozo de cuerpo: elipsoide en el espacio de una articulación.
struct EnemyShape {
    int joint;
    Vec3 center, radii;
    uint32_t color;
    bool glow;  // emisivo (ojos, puntas brillantes)
};

struct EnemyDef {
    const char* name;
    const JointDef* rig;
    int jointCount;
    const EnemyShape* shapes;
    int shapeCount;
    float maxHealth;
    float speed;          // m/s persiguiendo
    float radius, height; // cuerpo para colisiones
    float aggroRange;     // a esta distancia empieza a perseguir
    float attackRange;    // a esta distancia empieza a preparar el ataque
    float windup, attackTime, recover, cooldown;  // segundos
    float damage, knock;
    bool flying;
    float hoverHeight;    // voladores: altura del cuerpo sobre el suelo
    int munnyMin, munnyMax;
};

const EnemyDef& enemyDef(EnemyKind k);

struct Enemy {
    EnemyKind kind = EnemyKind::Imp;
    Body body;
    float yaw = 0;
    float health = 1;
    AiState state = AiState::Spawning;
    float stateTime = 0;
    float hitFlash = 0;   // destello blanco al recibir un golpe
    float cooldown = 0;   // hasta poder volver a atacar
    float freeze = 0;     // > 0: congelado (Hielo): todo a menos velocidad
    float burn = 0;       // > 0: ardiendo (Fuego): daño continuo
    float seed = 0;       // desfase de animación y de la deambulación
    float phase = 0;      // ciclo de pasos / aleteo
    bool attackHit = false;  // el ataque en curso ya ha golpeado
    bool boss = false;    // jefe: más grande y resistente; invoca ayuda
    float size = 1;       // escala del cuerpo (jefes)
    int bossPhase = 0;
    const char* title = nullptr;  // nombre propio (jefes)
    const char* name() const { return title ? title : def().name; }
    Vec3 knock;           // empujón recibido, se frena solo
    Pose pose;
    Mat4 joints[kMaxJoints] = {};
    // Fluidez: fundido de pose al cambiar de estado, apéndices con inercia (un punto con
    // muelle que persigue al cuerpo) y estado anterior para interpolar el render.
    Pose blendFrom;
    float blendTime = 1;
    AiState lastState = AiState::Spawning;
    Vec3 lagPoint, lagVel;
    bool lagInit = false;
    Mat4 prevJoints[kMaxJoints] = {};
    Vec3 prevPos;

    const EnemyDef& def() const { return enemyDef(kind); }
    bool alive() const { return state != AiState::Dying; }
    bool targetable() const { return state != AiState::Dying && state != AiState::Spawning; }
    Vec3 forward() const { return {std::sin(yaw), 0, std::cos(yaw)}; }
    // Centro del cuerpo (donde se apunta y se comprueban los golpes).
    Vec3 center() const;
    Mat4 modelMatrix() const;
};

Enemy makeEnemy(EnemyKind kind, Vec3 pos, float yaw, float seed);
// Postura en la que se genera la malla del cuerpo: brazos y piernas separados del tronco
// para que la unión suave no los pegue.
Pose enemyBindPose(EnemyKind kind);
// Pose según el estado y el tiempo; calcula también las matrices de las articulaciones.
void animateEnemy(Enemy& e, float time, float dt);
