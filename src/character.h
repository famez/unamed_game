#pragma once

#include "physics.h"
#include "skeleton.h"
#include "vmath.h"

enum class Item { Fists, Sword, Pistol, Rocks, Molotov, Count };
enum class Action { None, Punch, Slash, Shoot, Throw };

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

    Body body;
    float yaw = 0;    // orientación; 0 = mirando hacia +Z
    float speed = 0;  // velocidad horizontal actual (m/s)
    Item item = Item::Fists;
    Pose pose;
    Mat4 joints[JointCount] = {};  // matrices mundo tras el último update

    // `moveDir` en espacio mundo (plano XZ), longitud 0..1.
    void update(float dt, Vec3 moveDir, bool run, bool jump, const World& world);
    void reset();

    // Apuntado mantenido (botón derecho) hacia un punto del mundo: el cuerpo se gira hacia
    // él, el brazo lo apunta exactamente (cinemática inversa) y se camina más despacio.
    // Llamar antes de update().
    void setAim(bool held, Vec3 target);

    // Empieza una acción mirando hacia `faceYaw`. Falla si hay otra en curso.
    bool startAction(Action a, float faceYaw);
    // Devuelve la acción cuyo instante de impacto acaba de pasar (o None), una sola vez.
    Action takeImpact();

    Action action() const { return action_; }
    bool actionImpactDone() const { return impactDone_; }
    bool aiming() const { return aimTimer_ > 0 || aimHeld_; }
    bool aimHeld() const { return aimHeld_; }
    // Punto al que apunta el brazo: el de la mira, o 30 m al frente si dispara desde la cadera.
    Vec3 aimTarget() const { return aimTarget_; }

    Mat4 modelMatrix() const { return translate(body.pos) * rotateY(yaw); }
    Vec3 forward() const { return {std::sin(yaw), 0, std::cos(yaw)}; }

private:
    void animate();
    void aimArm();
    float aimWeight() const;

    Vec3 moveDir_{0, 0, 1};  // última dirección de movimiento pedida
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
