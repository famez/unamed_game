#pragma once

#include "physics.h"
#include "skeleton.h"
#include "vmath.h"

enum class Item { Fists, Sword, Pistol, Rocks, Molotov, Shotgun, Grenade, Hammer, Count };
// Los golpes cuerpo a cuerpo forman combos de 3: puños (directo, izquierdo, gancho) y
// espada (tajo vertical, barrido horizontal, estocada).
// ShootHeavy: escopeta (más retroceso y recarga). Smash: martillazo.
enum class Action { None, Punch, Slash, Shoot, Throw, Punch2, Punch3, Slash2, Slash3, ShootHeavy, Smash };
// Apuntado con botón derecho: con arma de fuego (brazo por cinemática inversa) o para
// lanzar (brazo atrás preparado, se ve la trayectoria).
enum class AimMode { None, Gun, Throw };

// Personaje controlable. La animación es procedural (senos/cosenos para andar) más
// una pequeña tabla de keyframes por acción (golpe, espadazo, disparo, lanzamiento)
// que se mezcla encima del brazo derecho.
class Character {
public:
    static constexpr float kWalkSpeed = 3.5f;   // m/s (trote)
    static constexpr float kRunSpeed = 8.0f;    // m/s (sprint)
    static constexpr float kAimSpeed = 2.2f;    // m/s al apuntar
    static constexpr float kJumpSpeed = 7.0f;   // m/s → ~1,2 m de altura con kGravity
    static constexpr float kStepHeight = 0.3f;  // bordillo que se sube sin saltar
    static constexpr float kBackSpeedFactor = 0.75f;  // andar hacia atrás es más lento
    static constexpr float kRollSpeed = 7.5f, kRollDuration = 0.55f;
    static constexpr float kComboWindow = 0.45f;  // tras un golpe, para encadenar el siguiente

    Body body;
    float yaw = 0;    // orientación; 0 = mirando hacia +Z
    float speed = 0;  // velocidad horizontal actual (m/s)
    Item item = Item::Fists;
    float downTimer = -1;  // >= 0: derribado (cae de espaldas)
    Pose pose;
    Mat4 joints[JointCount] = {};  // matrices mundo tras el último update

    // `moveDir` en espacio mundo (plano XZ), longitud 0..1. `faceYaw` es hacia donde
    // mira la cámara: el personaje se orienta siempre hacia ahí y se desplaza de lado o
    // hacia atrás según `moveDir`. Saltar mientras se va de lado hace una voltereta.
    void update(float dt, Vec3 moveDir, bool run, bool jump, float faceYaw, const World& world);
    void reset();

    // Apuntado mantenido (botón derecho) hacia un punto del mundo: el cuerpo se gira hacia
    // él y se camina más despacio. Con arma de fuego el brazo lo apunta exactamente
    // (cinemática inversa); para lanzar, el brazo queda atrás preparado.
    // Llamar antes de update().
    void setAim(AimMode mode, Vec3 target);

    // Empieza una acción mirando hacia `faceYaw`, desde la fracción `startU` de su
    // animación (un lanzamiento apuntado ya tiene el brazo atrás). Falla si hay otra en
    // curso o está rodando.
    bool startAction(Action a, float faceYaw, float startU = 0);
    // Ataque cuerpo a cuerpo con puños o espada: el siguiente golpe del combo. Si llega
    // durante un golpe se guarda y se encadena en cuanto este termina.
    bool attack(Item weapon, float faceYaw);
    // Defensa con la espada (botón derecho). Llamar antes de update().
    void setBlock(bool held) { blockHeld_ = held; }
    bool blocking() const { return blockHeld_ && item == Item::Sword && action_ == Action::None && !rolling(); }
    // Empujón recibido (m/s), se frena solo.
    void push(Vec3 v) { knock_ += v; }
    // Devuelve la acción cuyo instante de impacto acaba de pasar (o None), una sola vez.
    Action takeImpact();

    Action action() const { return action_; }
    bool actionImpactDone() const { return impactDone_; }
    bool aiming() const { return aimTimer_ > 0 || aimHeld_; }
    bool aimHeld() const { return aimHeld_; }  // apuntando con arma de fuego
    bool throwAimHeld() const { return throwAim_; }
    bool rolling() const { return rollTime_ >= 0; }
    // Punto al que apunta el brazo: el de la mira, o 30 m al frente si dispara desde la cadera.
    Vec3 aimTarget() const { return aimTarget_; }

    Mat4 modelMatrix() const;
    Vec3 forward() const { return {std::sin(yaw), 0, std::cos(yaw)}; }

private:
    void animate();
    void aimArm();
    float aimWeight() const;

    Vec3 moveDir_{0, 0, 1};  // última dirección de movimiento pedida
    float rollTime_ = -1;    // >= 0: rodando (segundos desde el inicio)
    Vec3 rollDir_;
    Action lastAttack_ = Action::None;  // último golpe del combo en curso
    float comboTimer_ = 0;              // segundos desde que acabó
    bool comboQueued_ = false;
    float queuedYaw_ = 0;
    bool blockHeld_ = false;
    float blockBlend_ = 0;
    Vec3 knock_;
    bool throwAim_ = false;
    float throwAimBlend_ = 0;
    float phase_ = 0;        // fase del ciclo de paso (radianes)
    float time_ = 0;
    float airBlend_ = 0;     // 0 = en el suelo, 1 = en el aire
    Action action_ = Action::None;
    Action pendingImpact_ = Action::None;
    float actionTime_ = 0;
    bool impactDone_ = false;
    float aimTimer_ = 0;     // mantiene el brazo apuntando tras disparar
    bool aimHeld_ = false;
    Vec3 aimTarget_;
};
