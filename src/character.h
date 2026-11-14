#pragma once

#include "physics.h"
#include "skeleton.h"
#include "vmath.h"

// Movimientos de ataque. Combo de suelo según el arma (hoja: tajo, revés, giro; lanza:
// estocadas; guanteletes: puñetazos) y su remate; combo aéreo de 2 golpes y remolino.
// Especiales: embestida (atacar corriendo), contraataque (justo tras esquivar), corte
// cargado (mantener el clic), lanzador (atacar con la guardia: sube al enemigo y al
// personaje) y picado (en el aire con la guardia: cae en picado). Cast: técnica.
enum class Move {
    None, Slash1, Slash2, Slash3, Finisher, Air1, Air2, AirFinisher, Cast,
    Rush, Counter, Charged, Launcher, Plunge, Thrust, Punch, PunchL, Uppercut,
    Count
};

// Juego de movimientos del arma equipada.
enum class MoveSet { Blade, Lance, Fists };

inline bool isAttack(Move m) { return m != Move::None && m != Move::Cast; }
inline bool isAirAttack(Move m) { return m == Move::Air1 || m == Move::Air2 || m == Move::AirFinisher; }
inline bool isFinisher(Move m) {
    return m == Move::Finisher || m == Move::AirFinisher || m == Move::Uppercut || m == Move::Charged || m == Move::Plunge;
}

// Protagonista. Se mueve hacia donde se le dirige y su animación es procedural: ciclo de
// carrera con senos más keyframes por movimiento con curvas de aceleración (preparación,
// golpe seco, rebote). Para que nada cambie de golpe:
//   - al cambiar de estado (golpe, voltereta, aterrizaje...) la pose se funde desde la
//     anterior en una fracción de segundo;
//   - el cuerpo se inclina al acelerar y al girar, y se aplasta al aterrizar;
//   - el pelo y la cinta de la espalda se mueven por inercia (muelles y Verlet).
class Character {
public:
    static constexpr float kRunSpeed = 6.5f;    // m/s
    static constexpr float kSprintSpeed = 10.0f; // corriendo con Shift
    static constexpr float kGuardSpeed = 2.6f;   // andando con la guardia levantada
    static constexpr float kChargeTime = 0.55f;  // mantener el clic para el corte cargado
    static constexpr float kJumpSpeed = 8.0f;   // m/s → ~1,6 m de altura con kGravity
    static constexpr float kStepHeight = 0.3f;  // bordillo que se sube sin saltar
    static constexpr float kRollSpeed = 9.0f, kRollDuration = 0.5f;
    static constexpr float kComboWindow = 0.5f;  // tras un golpe, para encadenar el siguiente
    static constexpr int kRibbonPoints = 7;      // cinta que cuelga de la espalda

    Body body;
    float yaw = 0;    // orientación; 0 = mirando hacia +Z
    float speed = 0;  // velocidad horizontal actual (m/s)
    float downTimer = -1;  // >= 0: derrotado (se desploma)
    float lying = 0;       // 1 = tumbado boca arriba (al despertar), 0 = de pie
    int comboHits = 3;     // golpes en el suelo antes del remate (lo fija el arma)
    MoveSet moveSet = MoveSet::Blade;
    float attackSpeed = 1; // multiplicador de la velocidad de los golpes (arma)
    Pose pose;             // pose mostrada (ya mezclada)
    Mat4 joints[JointCount] = {};  // matrices mundo tras el último update

    // `moveDir` en el mundo (plano XZ), longitud 0..1. `camYaw`: hacia dónde mira la
    // cámara (con la guardia o cargando, el personaje se orienta hacia ahí).
    void update(float dt, Vec3 moveDir, bool jump, const World& world, float camYaw = 0);
    // Correr (mantener Shift) y botón de ataque mantenido (para cargar). Antes de update().
    void setSprint(bool held) { sprintHeld_ = held; }
    void setAttackHeld(bool held) { attackHeld_ = held; }
    bool sprinting() const { return sprintHeld_ && body.grounded && speed > kRunSpeed + 0.5f; }
    float charge() const { return chargeTime_ > 0.15f ? clampf((chargeTime_ - 0.15f) / kChargeTime, 0, 1) : 0.0f; }
    void reset();

    // Siguiente golpe del combo hacia `faceYaw`. `dash` (m) acerca al personaje al objetivo
    // al empezar el golpe. Si llega durante otro golpe queda en cola y se encadena.
    bool attack(float faceYaw, float dash);
    bool cast(float faceYaw);
    // La Luz. Absorber (mantener): quieto, brazos abiertos. Haz: el cuerpo se vuelve luz y
    // sale disparado en línea recta sin gravedad (invulnerable, atraviesa a los Grises).
    void setAbsorb(bool held) { absorbHeld_ = held; }
    bool absorbing() const;
    bool startBeam(Vec3 dir, float duration);
    bool beaming() const { return beamTime_ > 0; }
    Vec3 beamDir() const { return beamDir_; }
    static constexpr float kBeamSpeed = 32.0f;
    // Ventana de contraataque tras una esquiva.
    bool counterReady() const { return counterWindow_ > 0; }
    // Voltereta de esquiva en la dirección dada: invulnerable mientras dura.
    bool dodge(Vec3 dir);
    // Guardia (mantener). Llamar antes de update().
    void setGuard(bool held) { guardHeld_ = held; }
    bool guarding() const;
    float guardTime() const { return guardTime_; }  // segundos con la guardia levantada
    // Empujón recibido (m/s), se frena solo.
    void push(Vec3 v) { knock_ += v; }
    // Golpe recibido: interrumpe lo que haga y se encoge un momento.
    void flinch(float seconds);
    // Movimiento cuyo instante de impacto acaba de pasar (o None), una sola vez.
    Move takeImpact();

    Move move() const { return move_; }
    float moveU() const;  // 0..1 dentro del movimiento en curso
    bool rolling() const { return rollTime_ >= 0; }
    bool hurt() const { return hurtTimer_ > 0; }
    bool busy() const {
        return move_ != Move::None || rolling() || hurt() || downTimer >= 0 || lying > 0.01f || beaming();
    }
    bool airComboSpent() const { return airComboSpent_; }
    // Acercamiento rápido en curso (para abrir un poco el campo de visión).
    float dashAmount() const { return dashTime_ > 0 ? 1.0f : 0.0f; }

    Mat4 modelMatrix() const;
    Vec3 forward() const { return {std::sin(yaw), 0, std::cos(yaw)}; }

    // Interpolación para el render: el estado entre el paso anterior (alpha = 0) y el
    // actual (alpha = 1). Así el movimiento es suave a cualquier tasa de refresco.
    void interpolatedJoints(float alpha, Mat4 out[JointCount]) const;
    Vec3 interpolatedPos(float alpha) const { return prevPos_ + (body.pos - prevPos_) * alpha; }
    // Desfase del pelo por inercia (mundo) y puntos de la cinta (mundo).
    Vec3 hairLag() const { return hairOffset_; }
    void ribbon(float alpha, Vec3 out[kRibbonPoints]) const;

private:
    void startMove(Move m, float faceYaw, float dash);
    Move nextAttack() const;
    void animate(Pose& target) const;
    int stateKey() const;
    void updateSecondary(float dt);

    Vec3 moveDir_{0, 0, 1};
    float rollTime_ = -1;
    Vec3 rollDir_;
    float hurtTimer_ = 0;
    bool guardHeld_ = false;
    float guardTime_ = 0;
    float guardBlend_ = 0;
    Vec3 knock_;
    Vec3 dashVel_;
    float dashTime_ = 0;
    float phase_ = 0;
    float time_ = 0;
    float airBlend_ = 0;
    Move move_ = Move::None;
    Move pendingImpact_ = Move::None;
    float moveTime_ = 0;
    bool impactDone_ = false;
    int comboStep_ = 0;      // golpe del combo de suelo que toca
    int airStep_ = 0;        // y del aéreo
    bool airComboSpent_ = false;  // ya hizo el remate aéreo en este salto
    float comboTimer_ = 10;  // segundos desde el último golpe
    bool queued_ = false;
    float queuedYaw_ = 0, queuedDash_ = 0;
    Move queuedMove_ = Move::None;
    bool sprintHeld_ = false, attackHeld_ = false;
    float chargeTime_ = 0;       // segundos con el ataque mantenido sin hacer nada más
    float counterWindow_ = 0;    // tras esquivar, para el contraataque
    bool doubleJumpUsed_ = false;
    bool absorbHeld_ = false;
    float absorbBlend_ = 0;
    float beamTime_ = 0;
    Vec3 beamDir_{0, 0, 1};
    float flipTime_ = -1;        // voltereta del doble salto
    float camYaw_ = 0;

    // Fluidez.
    Pose blendFrom_;         // pose de la que se parte al cambiar de estado
    float blendTime_ = 1, blendDur_ = 0.1f;
    int lastKey_ = -1;
    float lean_ = 0, bank_ = 0;  // inclinación hacia delante (aceleración) y lateral (giro)
    float prevSpeed_ = 0, prevYaw_ = 0;
    float landTimer_ = 0, landStrength_ = 0;
    bool wasGrounded_ = true;
    float fallSpeed_ = 0;
    Mat4 prevJoints_[JointCount] = {};
    Vec3 prevPos_;
    // Movimiento secundario.
    Vec3 hairPoint_, hairVel_, hairOffset_;
    Vec3 ribbon_[kRibbonPoints], ribbonOld_[kRibbonPoints], ribbonPrevFrame_[kRibbonPoints];
    bool secondaryInit_ = false;
};
