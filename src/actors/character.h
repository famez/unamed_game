#pragma once

#include "actors/physics.h"
#include "actors/skeleton.h"
#include "core/vmath.h"

// Movimientos de ataque. Combo de suelo según el arma (hoja: tajo, revés, giro; lanza:
// estocadas; guanteletes: puñetazos) y su remate; combo aéreo de 2 golpes y remolino.
// Especiales: embestida (atacar corriendo), contraataque (justo tras esquivar), corte
// cargado (mantener el clic), lanzador (atacar con la guardia: sube al enemigo y al
// personaje) y picado (en el aire con la guardia: cae en picado). Cast: técnica.
// Martillo (sombra): mazazo de arriba abajo, barrido en media vuelta y, de remate, un salto
// que cae con el martillo y agrieta el suelo (Quake).
enum class Move {
    None, Slash1, Slash2, Slash3, Finisher, Air1, Air2, AirFinisher, Cast,
    Rush, Counter, Charged, Launcher, Plunge, Thrust, Punch, PunchL, Uppercut,
    Smash, Sweep, Quake,
    Count
};

// Juego de movimientos del arma equipada.
enum class MoveSet { Blade, Lance, Fists, Hammer };

inline bool isAttack(Move m) { return m != Move::None && m != Move::Cast; }
inline bool isAirAttack(Move m) { return m == Move::Air1 || m == Move::Air2 || m == Move::AirFinisher; }
inline bool isFinisher(Move m) {
    return m == Move::Finisher || m == Move::AirFinisher || m == Move::Uppercut || m == Move::Charged || m == Move::Plunge ||
           m == Move::Quake;
}

// Protagonista. Se mueve hacia donde se le dirige y su animación es procedural: ciclo de
// carrera con senos más keyframes por movimiento con curvas de aceleración (preparación,
// golpe seco, rebote). Para que nada cambie de golpe:
//   - al cambiar de estado (golpe, voltereta, aterrizaje...) la pose se funde desde la
//     anterior en una fracción de segundo;
//   - cada articulación sigue a su pose con un muelle amortiguado (las piernas, firmes; el
//     tronco, los brazos y la cabeza, con algo de retraso y de rebote): los cambios bruscos se
//     redondean y los golpes tienen inercia;
//   - en los golpes el cuerpo se mueve en látigo: la cadera y el pecho se adelantan, y el
//     codo y la muñeca llegan un poco después que el hombro;
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
    // Modificadores de las formas prismáticas.
    float speedScale = 1;     // velocidad de carrera
    float gravityScale = 1;   // gravedad en el aire (Eco: casi nada)
    bool infiniteJumps = false;
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
    // Lo pone en otro sitio de golpe (sin interpolar desde donde estaba).
    void teleport(Vec3 pos, float newYaw);
    // Lo desplaza sin física (trepando como sombra): interpola desde donde estaba.
    void slide(Vec3 pos) {
        prevPos_ = body.pos;
        body.pos = pos;
        body.vel = {};
        body.grounded = false;
    }

    // Siguiente golpe del combo hacia `faceYaw`. `dash` (m) acerca al personaje al objetivo
    // al empezar el golpe. Si llega durante otro golpe queda en cola y se encadena.
    bool attack(float faceYaw, float dash);
    bool cast(float faceYaw);
    // Empieza ya este golpe, corte lo que esté haciendo (súper contraataque).
    void forceMove(Move m, float faceYaw, float dash) {
        queued_ = false;
        move_ = Move::None;
        startMove(m, faceYaw, dash);
    }
    // Arma en la mano (combate) o a la espalda (explorando). Cambiar hace el gesto de
    // llevarse la mano al hombro; a mitad del gesto el arma cambia de sitio.
    void setArmed(bool a, bool instant = false) {
        if (a == armed_) return;
        armed_ = a;
        drawTime_ = instant ? kDrawTime : 0.0f;
    }
    bool armed() const { return armed_; }
    // En guardia (enemigo cerca o fijado): postura de combate al estar quieto. Si no, con el
    // arma en la mano pero relajado (la hoja baja, a un lado).
    void setAlert(bool a) { alert_ = a; }
    bool weaponInHand() const { return drawTime_ < kDrawTime * 0.5f ? !armed_ : armed_; }
    static constexpr float kDrawTime = 0.35f;
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
    // Golpe recibido: interrumpe lo que haga y se encoge un momento. `push`: hacia dónde
    // empuja el golpe (mundo); el tronco se dobla hacia ese lado y rebota.
    void flinch(float seconds, Vec3 push = {});
    // Movimiento cuyo instante de impacto acaba de pasar (o None), una sola vez.
    Move takeImpact();

    Move move() const { return move_; }
    float moveU() const;  // 0..1 dentro del movimiento en curso
    bool rolling() const { return rollTime_ >= 0; }
    bool hurt() const { return hurtTimer_ > 0; }
    bool busy() const {
        return move_ != Move::None || rolling() || hurt() || downTimer >= 0 || lying > 0.01f || climbing();
    }

    // Trepar. Escaleras de gato (World::ladders): empujando hacia una se agarra; adelante sube,
    // atrás baja, arriba se encarama al borde y saltar se suelta de espaldas. Andando hacia el
    // borde donde acaba una, se baja por ella. Paredes: saltando contra una cara alta se queda
    // pegada un momento (luego resbala) y saltar desde ahí impulsa hacia fuera y hacia arriba;
    // la misma cara no vuelve a agarrarse hasta pisar suelo (entre dos paredes enfrentadas, sí
    // se sube). Si al llegar el borde le queda a la altura del pecho, se encarama.
    enum class Climb { None, Ladder, Wall, Mount };
    enum class ClimbEvent { None, Grab, Cling, WallJump, LadderJump, Mount };
    static constexpr float kLadderSpeed = 2.4f;
    static constexpr float kClingHold = 0.5f, kClingTime = 1.6f;  // quieta en la pared; y hasta soltarse
    static constexpr float kClingSlide = 2.2f;                    // m/s resbalando
    static constexpr float kWallJumpUp = 8.5f, kWallJumpOut = 6.5f;
    static constexpr float kMountTime = 0.42f;
    Climb climb() const { return climb_; }
    bool climbing() const { return climb_ != Climb::None; }
    float climbTime() const { return climbTime_; }  // segundos en lo que esté trepando
    float clingLeft() const { return climb_ == Climb::Wall ? clampf(1.0f - climbTime_ / kClingTime, 0, 1) : 0.0f; }
    // Lo que acaba de pasar trepando (para el sonido), una sola vez.
    ClimbEvent takeClimbEvent() {
        ClimbEvent e = event_;
        event_ = ClimbEvent::None;
        return e;
    }
    // Lo suelta de lo que esté trepando (cae).
    void releaseClimb();
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
    bool tryGrab(float input, const World& world);
    void updateClimb(float dt, float input, bool jump, const World& world);
    void startMount(Vec3 to, Climb after);
    void animateClimb(Pose& out) const;
    void finishPose(float dt);
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
    float flipTime_ = -1;        // voltereta del doble salto
    float camYaw_ = 0;

    // Trepar.
    Climb climb_ = Climb::None;
    int ladder_ = -1;
    AABB wallBox_{};          // la caja de la pared a la que está pegada
    Vec3 wallN_;              // su cara (hacia fuera)
    AABB lastWall_{};         // la última cara de la que se soltó (no se repite sin pisar suelo)
    Vec3 lastWallN_;
    bool lastWallSet_ = false;
    float climbTime_ = 0;     // segundos en la pared o en la escalera
    float climbPhase_ = 0;    // ciclo de manos y pies en la escalera
    float climbSpeed_ = 0;    // lo que sube (+) o baja (-) ahora
    Vec3 mountFrom_, mountTo_;
    Climb mountAfter_ = Climb::None;
    float regrab_ = 0;        // tras soltarse, un momento sin volver a agarrarse
    float jumpLock_ = 0;      // tras el salto de pared, la marcha sigue el impulso
    float airTime_ = 0;
    ClimbEvent event_ = ClimbEvent::None;

    // Fluidez.
    Pose blendFrom_;         // pose de la que se parte al cambiar de estado
    float blendTime_ = 1, blendDur_ = 0.1f;
    int lastKey_ = -1;
    float lean_ = 0, bank_ = 0;  // inclinación hacia delante (aceleración) y lateral (giro)
    float prevSpeed_ = 0, prevYaw_ = 0;
    float turnStep_ = 0;  // pasitos al girar casi parado (0..1)
    Vec3 moveVel_;        // velocidad propia en el plano (con inercia)
    bool armed_ = true;
    float drawTime_ = 1.0f, armedBlend_ = 1.0f;
    bool alert_ = false;
    float alertBlend_ = 0;
    // Muelles de las articulaciones: la pose mostrada persigue a la calculada.
    Pose sprung_;
    Vec3 springVel_[JointCount] = {};
    Vec3 rootSpringVel_;
    bool springInit_ = false;
    void applySprings(float dt);
    // Animación limitada (como el anime): en los golpes la pose se renueva a 20 fps y se
    // mantiene entre medias. Desactivada: con los muelles, los golpes fluyen.
    static constexpr bool kLimitedAnimation = false;
    bool stepping_ = false;
    Pose heldPose_;
    float holdTimer_ = 0;
    float idleTime_ = 0;  // segundos sin hacer nada (para el reposo)
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
