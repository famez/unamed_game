#include "character.h"

#include <algorithm>

namespace {

constexpr float kAcceleration = 45.0f;  // m/s²: arranca rápido pero no de golpe
constexpr float kDeceleration = 60.0f;
constexpr float kTurnSmooth = 16.0f;    // giro amortiguado (más alto = más rápido)
constexpr float kDashTime = 0.14f;      // el acercamiento al objetivo dura esto
constexpr float kAirHover = 0.85f;      // fracción de la gravedad que se anula en un golpe aéreo

// Curva de cada tramo entre keyframes. Un golpe con carácter no es una curva suave de
// principio a fin: se prepara (y se sostiene un instante arriba), golpea muy rápido y se
// pasa un poco de largo antes de volver.
enum Ease { Smooth, Windup, Strike, Overshoot };

float ease(Ease e, float t) {
    t = clampf(t, 0, 1);
    switch (e) {
        case Windup: return 1.0f - (1.0f - t) * (1.0f - t) * (1.0f - t);  // sale rápido, llega despacio
        case Strike: {
            float k = 1.0f - t;
            return 1.0f - k * k * k * k;  // casi todo el recorrido al principio
        }
        case Overshoot: {
            const float c1 = 1.70158f, c3 = c1 + 1.0f;
            float k = t - 1.0f;
            return 1.0f + c3 * k * k * k + c1 * k * k;
        }
        default: return t * t * (3.0f - 2.0f * t);
    }
}

// Pose del brazo del arma (derecho) y giro del pecho en un instante `u` ∈ [0,1].
// `ease` es la curva del tramo que llega a esta clave.
struct ArmKey {
    float u;
    float shoulderX, shoulderY, shoulderZ, elbowX, wristX, chestY;
    Ease ease;
};

// Un movimiento: keyframes del brazo + lo que hace el cuerpo entero.
struct MoveDef {
    float duration;      // segundos (los golpes se aceleran con la velocidad del arma)
    float impactU;       // momento en que el golpe tiene efecto
    float blendIn, blendOut;
    const ArmKey* keys;
    int keyCount;
    float spin;          // giro extra del cuerpo sobre su eje vertical (rad)...
    float flip;          // ...y voltereta hacia delante (rad)
    float turnFrom, turnTo;  // tramo (u) en que se dan el giro y la voltereta
    float hop;           // impulso vertical al empezar (m/s)
    float lean;          // inclinación del tronco hacia delante
    float step;          // paso adelante (m) durante el golpe...
    float stepFrom, stepTo;  // ...en este tramo
    bool leftArm = false;  // los keyframes son del brazo izquierdo (en espejo)
};

// Recordatorio de signos: rotar +X lleva hacia atrás un brazo que cuelga; -X hacia
// delante (-1,57 = horizontal al frente). -Z lo abre hacia la derecha del personaje.
// La hoja sale de la mano perpendicular al antebrazo (+Z local de la muñeca); con la
// muñeca a +1,3 queda en la prolongación del brazo.
#define GUARD -0.30f, 0.00f, -0.10f, -1.30f, 0.4f, 0.00f
// clang-format off
//                                u     hombroX hombroY hombroZ codoX  muñecaX pechoY  curva
const ArmKey kSlash1Keys[] = {  // barrido de derecha a izquierda
    {0.00f, GUARD, Smooth},
    {0.30f, -1.20f, -1.55f,  0.05f, -0.45f, 1.3f, -0.75f, Windup},
    {0.42f, -1.40f,  0.90f,  0.00f, -0.15f, 1.3f,  0.75f, Strike},
    {0.62f, -1.30f,  1.15f,  0.00f, -0.25f, 1.2f,  0.85f, Overshoot},
    {1.00f, GUARD, Smooth},
};
const ArmKey kSlash2Keys[] = {  // revés de izquierda a derecha
    {0.00f, -1.30f,  1.00f,  0.00f, -0.30f, 1.3f,  0.70f, Smooth},
    {0.25f, -1.50f,  1.25f,  0.00f, -0.50f, 1.3f,  0.85f, Windup},
    {0.40f, -1.30f, -1.40f,  0.00f, -0.20f, 1.3f, -0.75f, Strike},
    {0.60f, -1.20f, -1.65f,  0.00f, -0.30f, 1.2f, -0.85f, Overshoot},
    {1.00f, GUARD, Smooth},
};
const ArmKey kSpinKeys[] = {    // brazo abierto: el cuerpo gira y la hoja barre un círculo
    {0.00f, GUARD, Smooth},
    {0.15f, -0.40f,  0.00f, -1.45f, -0.10f, 1.3f, -0.30f, Windup},
    {0.80f, -0.40f,  0.00f, -1.45f, -0.10f, 1.3f, -0.30f, Smooth},
    {1.00f, GUARD, Smooth},
};
const ArmKey kOverheadKeys[] = {  // arma por encima de la cabeza y golpe hacia abajo
    {0.00f, GUARD, Smooth},
    {0.35f, -3.00f,  0.10f, -0.20f, -0.60f, 0.3f, -0.20f, Windup},
    {0.62f, -3.05f,  0.10f, -0.20f, -0.70f, 0.2f, -0.25f, Smooth},
    {0.72f, -0.60f,  0.00f, -0.10f,  0.00f, 1.2f,  0.30f, Strike},
    {0.85f, -0.45f,  0.00f, -0.10f,  0.10f, 1.3f,  0.35f, Overshoot},
    {1.00f, GUARD, Smooth},
};
const ArmKey kChopKeys[] = {    // tajo vertical (aire)
    {0.00f, GUARD, Smooth},
    {0.28f, -2.90f,  0.30f, -0.30f, -0.40f, 0.0f, -0.30f, Windup},
    {0.45f, -0.40f, -0.40f, -0.10f, -0.10f, 1.0f,  0.50f, Strike},
    {0.65f, -0.30f, -0.50f, -0.10f, -0.15f, 1.1f,  0.55f, Overshoot},
    {1.00f, GUARD, Smooth},
};
const ArmKey kCastKeys[] = {    // brazo al frente señalando con el arma
    {0.00f, GUARD, Smooth},
    {0.25f, -1.75f,  0.15f,  0.00f,  0.00f, 1.3f,  0.40f, Strike},
    {0.35f, -1.60f,  0.15f,  0.00f,  0.00f, 1.3f,  0.35f, Overshoot},
    {0.75f, -1.60f,  0.15f,  0.00f,  0.00f, 1.3f,  0.35f, Smooth},
    {1.00f, GUARD, Smooth},
};
const ArmKey kRushKeys[] = {     // embestida: hoja recta al frente todo el tiempo
    {0.00f, GUARD, Smooth},
    {0.15f, -1.55f,  0.00f,  0.00f,  0.00f, 1.45f, 0.50f, Strike},
    {0.80f, -1.55f,  0.00f,  0.00f,  0.00f, 1.45f, 0.50f, Smooth},
    {1.00f, GUARD, Smooth},
};
const ArmKey kChargedKeys[] = {  // corte cargado: ya viene con el arma arriba
    {0.00f, -3.00f,  0.10f, -0.20f, -0.60f, 0.3f, -0.20f, Smooth},
    {0.30f, -3.10f,  0.10f, -0.20f, -0.70f, 0.2f, -0.30f, Windup},
    {0.45f, -0.60f,  0.00f, -0.10f,  0.00f, 1.2f,  0.30f, Strike},
    {0.65f, -0.45f,  0.00f, -0.10f,  0.10f, 1.3f,  0.35f, Overshoot},
    {1.00f, GUARD, Smooth},
};
const ArmKey kLaunchKeys[] = {   // lanzador: de abajo a arriba
    {0.00f, GUARD, Smooth},
    {0.25f,  0.50f,  0.00f, -0.10f, -0.20f, 1.3f, -0.40f, Windup},
    {0.42f, -2.80f,  0.20f, -0.20f, -0.30f, 0.6f,  0.40f, Strike},
    {0.60f, -3.00f,  0.20f, -0.20f, -0.30f, 0.5f,  0.45f, Overshoot},
    {1.00f, GUARD, Smooth},
};
const ArmKey kPlungeKeys[] = {   // picado: arriba mientras cae y golpe contra el suelo
    {0.00f, GUARD, Smooth},
    {0.20f, -3.00f,  0.10f, -0.20f, -0.60f, 0.3f, -0.20f, Windup},
    {0.74f, -3.00f,  0.10f, -0.20f, -0.60f, 0.3f, -0.20f, Smooth},
    {0.80f, -0.50f,  0.00f, -0.10f,  0.00f, 1.2f,  0.30f, Strike},
    {1.00f, GUARD, Smooth},
};
const ArmKey kThrustKeys[] = {   // estocada (lanza)
    {0.00f, GUARD, Smooth},
    {0.30f, -0.60f,  0.10f, -0.20f, -1.80f, 1.3f, -0.40f, Windup},
    {0.45f, -1.55f,  0.00f,  0.00f,  0.00f, 1.45f, 0.50f, Strike},
    {0.62f, -1.60f,  0.00f,  0.00f,  0.05f, 1.45f, 0.55f, Overshoot},
    {1.00f, GUARD, Smooth},
};
#define FIST -0.30f, 0.00f, -0.10f, -1.80f, 0.0f, 0.00f
const ArmKey kPunchKeys[] = {    // directo (guanteletes); el izquierdo usa las mismas en espejo
    {0.00f, FIST, Smooth},
    {0.40f, -1.55f,  0.00f, -0.05f, -0.05f, 0.0f,  0.35f, Strike},
    {1.00f, FIST, Smooth},
};
const ArmKey kUppercutKeys[] = { // gancho final
    {0.00f, FIST, Smooth},
    {0.30f,  0.35f,  0.00f, -0.15f, -1.90f, 0.0f, -0.35f, Windup},
    {0.45f, -2.20f,  0.25f, -0.05f, -1.30f, 0.0f,  0.45f, Strike},
    {1.00f, FIST, Smooth},
};
#undef FIST
#undef GUARD

#define N(a) int(sizeof(a) / sizeof((a)[0]))
const MoveDef kMoves[int(Move::Count)] = {
//   dur    impacto entrada salida keys                       giro       volt.     tramo        salto lean   paso  tramo paso
    {0.00f, 0.00f, 0.00f, 0.00f, nullptr,       0,              0,         0,        0.00f, 1.00f, 0.0f, 0.00f, 0.0f, 0.0f, 1.0f},  // None
    {0.42f, 0.38f, 0.05f, 0.25f, kSlash1Keys,   N(kSlash1Keys), 0,         0,        0.00f, 1.00f, 0.0f, 0.18f, 0.4f, 0.3f, 0.45f}, // Slash1
    {0.42f, 0.36f, 0.04f, 0.25f, kSlash2Keys,   N(kSlash2Keys), 0,         0,        0.00f, 1.00f, 0.0f, 0.18f, 0.4f, 0.25f, 0.4f}, // Slash2
    {0.52f, 0.45f, 0.05f, 0.20f, kSpinKeys,     N(kSpinKeys),   2 * kPi,   0,        0.12f, 0.70f, 0.0f, 0.10f, 0.3f, 0.1f, 0.6f},  // Slash3
    {0.80f, 0.70f, 0.05f, 0.20f, kOverheadKeys, N(kOverheadKeys), 0,       2 * kPi,  0.05f, 0.55f, 5.5f, 0.25f, 0.8f, 0.1f, 0.6f},  // Finisher
    {0.36f, 0.42f, 0.06f, 0.25f, kChopKeys,     N(kChopKeys),   0,         0,        0.00f, 1.00f, 0.0f, 0.20f, 0.0f, 0.0f, 1.0f},  // Air1
    {0.36f, 0.38f, 0.04f, 0.25f, kSlash1Keys,   N(kSlash1Keys), 0,         0,        0.00f, 1.00f, 0.0f, 0.10f, 0.0f, 0.0f, 1.0f},  // Air2
    {0.60f, 0.50f, 0.05f, 0.20f, kSpinKeys,     N(kSpinKeys),   4 * kPi,   0,        0.10f, 0.80f, 0.0f, 0.10f, 0.0f, 0.0f, 1.0f},  // AirFinisher
    {0.50f, 0.28f, 0.08f, 0.25f, kCastKeys,     N(kCastKeys),   0,         0,        0.00f, 1.00f, 0.0f, 0.00f, 0.0f, 0.0f, 1.0f},  // Cast
    {0.50f, 0.35f, 0.04f, 0.25f, kRushKeys,     N(kRushKeys),   0,         0,        0.00f, 1.00f, 0.0f, 0.45f, 0.0f, 0.0f, 1.0f},  // Rush
    {0.42f, 0.40f, 0.03f, 0.20f, kSpinKeys,     N(kSpinKeys),   2 * kPi,   0,        0.05f, 0.65f, 0.0f, 0.10f, 0.3f, 0.1f, 0.5f},  // Counter
    {0.55f, 0.47f, 0.04f, 0.20f, kChargedKeys,  N(kChargedKeys), 0,        0,        0.00f, 1.00f, 0.0f, 0.35f, 0.6f, 0.35f, 0.55f}, // Charged
    {0.55f, 0.42f, 0.05f, 0.25f, kLaunchKeys,   N(kLaunchKeys), 0,         0,        0.00f, 1.00f, 7.5f, -0.10f, 0.0f, 0.0f, 1.0f}, // Launcher
    {1.40f, 0.80f, 0.05f, 0.20f, kPlungeKeys,   N(kPlungeKeys), 0,         0,        0.00f, 1.00f, 0.0f, 0.30f, 0.0f, 0.0f, 1.0f},  // Plunge
    {0.42f, 0.42f, 0.05f, 0.25f, kThrustKeys,   N(kThrustKeys), 0,         0,        0.00f, 1.00f, 0.0f, 0.25f, 0.6f, 0.3f, 0.5f},  // Thrust
    {0.24f, 0.38f, 0.05f, 0.30f, kPunchKeys,    N(kPunchKeys),  0,         0,        0.00f, 1.00f, 0.0f, 0.15f, 0.2f, 0.2f, 0.5f},  // Punch
    {0.24f, 0.38f, 0.05f, 0.30f, kPunchKeys,    N(kPunchKeys),  0,         0,        0.00f, 1.00f, 0.0f, 0.15f, 0.2f, 0.2f, 0.5f, true}, // PunchL
    {0.50f, 0.45f, 0.05f, 0.25f, kUppercutKeys, N(kUppercutKeys), 0,       0,        0.00f, 1.00f, 3.5f, 0.10f, 0.2f, 0.2f, 0.45f}, // Uppercut
};
#undef N
// clang-format on

ArmKey lerpKey(const ArmKey& a, const ArmKey& b, float t) {
    return {0,
            lerpAngle(a.shoulderX, b.shoulderX, t),
            lerpAngle(a.shoulderY, b.shoulderY, t),
            lerpAngle(a.shoulderZ, b.shoulderZ, t),
            lerpAngle(a.elbowX, b.elbowX, t),
            lerpAngle(a.wristX, b.wristX, t),
            lerpf(a.chestY, b.chestY, t),
            Smooth};
}

ArmKey sampleKeys(const MoveDef& d, float u) {
    for (int i = 0; i + 1 < d.keyCount; ++i) {
        const ArmKey& a = d.keys[i];
        const ArmKey& b = d.keys[i + 1];
        if (u <= b.u) return lerpKey(a, b, ease(b.ease, (u - a.u) / (b.u - a.u)));
    }
    return d.keys[d.keyCount - 1];
}

// Mezcla la pose de un brazo sobre la de locomoción con peso `w`. El izquierdo, en espejo
// (se invierten los giros en Y y Z, y la torsión del pecho).
void applyArm(Vec3* r, const ArmKey& k, float w, bool left = false) {
    Joint sh = left ? ShoulderL : ShoulderR, el = left ? ElbowL : ElbowR, wr = left ? WristL : WristR;
    float m = left ? -1.0f : 1.0f;
    r[sh].x = lerpAngle(r[sh].x, k.shoulderX, w);
    r[sh].y = lerpAngle(r[sh].y, m * k.shoulderY, w);
    r[sh].z = lerpAngle(r[sh].z, m * k.shoulderZ, w);
    r[el].x = lerpAngle(r[el].x, k.elbowX, w);
    r[wr].x = lerpAngle(r[wr].x, k.wristX, w);
    r[Chest].y += m * k.chestY * w;
}

float moveDuration(Move m, float attackSpeed) {
    float d = kMoves[int(m)].duration;
    return isAttack(m) ? d / attackSpeed : d;
}

// Velocidad (m/s) del paso adelante de un golpe en el instante u: reparte `step` metros
// en su tramo con un perfil en campana.
float stepSpeed(const MoveDef& d, float u, float duration) {
    if (d.step <= 0 || u < d.stepFrom || u > d.stepTo) return 0;
    float len = (d.stepTo - d.stepFrom) * duration;
    float t = (u - d.stepFrom) / (d.stepTo - d.stepFrom);
    return d.step / len * (kPi * 0.5f) * std::sin(t * kPi);  // ∫ sin(πt) dt = 2/π: recorre `step` metros
}

}  // namespace

void Character::reset() { *this = Character{}; }

bool Character::absorbing() const {
    return absorbHeld_ && body.grounded && move_ == Move::None && !rolling() && !hurt() && downTimer < 0 &&
           lying <= 0.01f;
}

bool Character::guarding() const {
    return guardHeld_ && body.grounded && move_ == Move::None && !rolling() && !hurt() && downTimer < 0;
}

float Character::moveU() const {
    return move_ == Move::None ? 0.0f : clampf(moveTime_ / moveDuration(move_, attackSpeed), 0, 1);
}

Mat4 Character::modelMatrix() const {
    Mat4 m = translate(body.pos) * rotateY(yaw);
    if (downTimer >= 0) {  // se desploma de espaldas
        const Vec3 pivot{0, 0.1f, 0};
        return m * translate(pivot) * rotateX(-smoothstep01(downTimer / 0.6f) * kPi * 0.48f) * translate(-pivot);
    }
    if (lying > 0) {  // tumbado: pivota sobre los talones
        const Vec3 pivot{0, 0.1f, 0};
        m = m * translate(pivot) * rotateX(-smoothstep01(lying) * kPi * 0.48f) * translate(-pivot);
    }
    if (move_ != Move::None) {
        const MoveDef& d = kMoves[int(move_)];
        // El giro arranca fuerte y frena al final (la hoja "corta" en la primera vuelta).
        float t = ease(Strike, (moveU() - d.turnFrom) / (d.turnTo - d.turnFrom));
        if (d.spin != 0) m = m * rotateY(d.spin * t);
        if (d.flip != 0) {  // voltereta alrededor del centro del cuerpo
            const Vec3 pivot{0, 0.9f, 0};
            m = m * translate(pivot) * rotateX(d.flip * smoothstep01((moveU() - d.turnFrom) / (d.turnTo - d.turnFrom))) *
                translate(-pivot);
        }
    }
    if (flipTime_ >= 0) {  // doble salto: voltereta hacia delante
        const Vec3 pivot{0, 0.9f, 0};
        m = m * translate(pivot) * rotateX(smoothstep01(flipTime_ / 0.45f) * 2.0f * kPi) * translate(-pivot);
    }
    if (rolling()) {
        // Voltereta: el cuerpo encogido da una vuelta sobre el eje lateral.
        float u = clampf(rollTime_ / kRollDuration, 0, 1);
        const Vec3 pivot{0, 0.5f, 0};
        m = m * translate(pivot) * rotateX(smoothstep01(u) * 2.0f * kPi) * translate(-pivot);
    }
    return m;
}

Move Character::nextAttack() const {
    if (!body.grounded) {
        if (airComboSpent_) return Move::None;
        return airStep_ == 0 ? Move::Air1 : airStep_ == 1 ? Move::Air2 : Move::AirFinisher;
    }
    if (comboStep_ >= comboHits) return moveSet == MoveSet::Fists ? Move::Uppercut : Move::Finisher;
    switch (moveSet) {
        case MoveSet::Lance: {
            const Move seq[3] = {Move::Thrust, Move::Slash2, Move::Thrust};
            return seq[comboStep_ % 3];
        }
        case MoveSet::Fists: return comboStep_ % 2 ? Move::PunchL : Move::Punch;
        default: {
            const Move seq[3] = {Move::Slash1, Move::Slash2, Move::Slash3};
            return seq[comboStep_ % 3];
        }
    }
}

void Character::startMove(Move m, float faceYaw, float dash) {
    move_ = m;
    moveTime_ = 0;
    impactDone_ = false;
    yaw = wrapAngle(faceYaw);
    speed = 0;
    const MoveDef& d = kMoves[int(m)];
    if (isAirAttack(m)) {
        ++airStep_;
        if (m == Move::AirFinisher) airComboSpent_ = true;
        body.vel.y = std::max(body.vel.y, 2.0f);  // un pequeño impulso: el combo aéreo "flota"
    } else if (m == Move::Finisher || m == Move::Uppercut) {
        comboStep_ = 0;
    } else if (m == Move::Slash1 || m == Move::Slash2 || m == Move::Slash3 || m == Move::Thrust || m == Move::Punch ||
               m == Move::PunchL) {
        ++comboStep_;
    }
    if (d.hop > 0 && body.grounded) body.vel.y = d.hop;
    if (m == Move::Plunge) body.vel.y = std::max(body.vel.y, 3.0f);  // un saltito antes de caer
    if (m == Move::Rush) {  // embestida: sale disparado hacia delante
        dashTime_ = 0.28f;
        dashVel_ = forward() * 15.0f;
        return;
    }
    if (dash > 0.05f) {
        dashTime_ = kDashTime;
        dashVel_ = forward() * (dash / kDashTime);
    } else {
        dashTime_ = 0;
        dashVel_ = {};
    }
}

bool Character::attack(float faceYaw, float dash) {
    if (rolling() || hurt() || downTimer >= 0 || lying > 0.01f) return false;
    // Especiales según la situación (solo si no hay otro golpe en curso).
    if (move_ == Move::None) {
        Move special = Move::None;
        if (!body.grounded && guardHeld_) special = Move::Plunge;
        else if (body.grounded && counterWindow_ > 0) special = Move::Counter;
        else if (sprinting()) special = Move::Rush;
        else if (body.grounded && guardHeld_) special = Move::Launcher;
        if (special != Move::None) {
            counterWindow_ = 0;
            startMove(special, faceYaw, special == Move::Counter ? dash : 0.0f);
            return true;
        }
    }
    if (isAttack(move_)) {  // durante un golpe: se encadena el siguiente al terminar
        if (move_ == Move::AirFinisher || move_ == Move::Plunge || move_ == Move::Rush) return false;
        queued_ = true;
        queuedYaw_ = faceYaw;
        queuedDash_ = dash;
        return true;
    }
    if (move_ != Move::None) return false;
    Move m = nextAttack();
    if (m == Move::None) return false;
    startMove(m, faceYaw, dash);
    return true;
}

bool Character::cast(float faceYaw) {
    if (busy()) return false;
    startMove(Move::Cast, faceYaw, 0);
    return true;
}

bool Character::dodge(Vec3 dir) {
    if (rolling() || hurt() || downTimer >= 0 || !body.grounded) return false;
    // Se puede cancelar la recuperación de un golpe (no su preparación).
    if (move_ != Move::None && !impactDone_) return false;
    move_ = Move::None;
    queued_ = false;
    dashTime_ = 0;
    dashVel_ = {};
    Vec3 d = normalize(flat(dir));
    rollDir_ = length(d) > 0.5f ? d : forward();
    rollTime_ = 0;
    return true;
}

void Character::flinch(float seconds) {
    hurtTimer_ = seconds;
    move_ = Move::None;
    queued_ = false;
    dashTime_ = 0;
    dashVel_ = {};
}

Move Character::takeImpact() {
    Move m = pendingImpact_;
    pendingImpact_ = Move::None;
    return m;
}

// Estados "discretos" de la animación: al cambiar alguno se funde la pose.
int Character::stateKey() const {
    return int(move_) | (rolling() ? 1 << 5 : 0) | (body.grounded ? 1 << 6 : 0) | (guarding() ? 1 << 7 : 0) |
           (hurt() ? 1 << 8 : 0) | (downTimer >= 0 ? 1 << 9 : 0);
}

void Character::update(float dt, Vec3 moveDir, bool jump, const World& world, float camYaw) {
    camYaw_ = camYaw;
    counterWindow_ = std::max(0.0f, counterWindow_ - dt);
    if (flipTime_ >= 0 && (flipTime_ += dt) > 0.45f) flipTime_ = -1;
    for (int i = 0; i < JointCount; ++i) prevJoints_[i] = joints[i];
    prevPos_ = body.pos;
    for (int i = 0; i < kRibbonPoints; ++i) ribbonPrevFrame_[i] = ribbon_[i];
    time_ += dt;
    hurtTimer_ = std::max(0.0f, hurtTimer_ - dt);
    float input = downTimer >= 0 ? 0.0f : std::min(length(moveDir), 1.0f);
    if (input > 0.01f) moveDir_ = normalize(moveDir);

    // Saltar cancela la recuperación de un golpe de suelo (no su preparación).
    if (jump && body.grounded && isAttack(move_) && !isAirAttack(move_) && impactDone_ && moveU() > 0.55f) {
        move_ = Move::None;
        queued_ = false;
        dashTime_ = 0;
        dashVel_ = {};
    }

    // Cargar el corte: con el ataque mantenido y las manos libres. Al soltar con la carga
    // completa sale el corte cargado.
    bool charging = attackHeld_ && move_ == Move::None && body.grounded && !rolling() && !hurt() && !guardHeld_ &&
                    downTimer < 0 && lying <= 0.01f;
    if (charging) {
        chargeTime_ += dt;
    } else {
        if (!attackHeld_ && charge() >= 1.0f && move_ == Move::None) startMove(Move::Charged, camYaw_, 0);
        chargeTime_ = 0;
    }
    // Correr hacia donde se pide; el cuerpo gira hacia la dirección de la marcha con un
    // giro amortiguado. Con la guardia o cargando se anda despacio mirando a la cámara.
    bool canMove = !busy() && !absorbing();
    bool aimed = guarding() || charge() > 0;
    float maxSpeed = (aimed ? kGuardSpeed : (sprintHeld_ && body.grounded ? kSprintSpeed : kRunSpeed)) * speedScale;
    float targetSpeed = canMove ? maxSpeed * input : 0.0f;
    speed = moveTowards(speed, targetSpeed, (targetSpeed > speed ? kAcceleration : kDeceleration) * dt);
    if (canMove && (input > 0.01f || aimed)) {
        float targetYaw = aimed ? camYaw_ : std::atan2(moveDir_.x, moveDir_.z);
        float step = wrapAngle(targetYaw - yaw) * (1.0f - std::exp(-kTurnSmooth * dt));
        yaw = wrapAngle(yaw + clampf(step, -22.0f * dt, 22.0f * dt));
    }
    Vec3 dir = canMove || speed < 0.01f ? moveDir_ : forward();
    if (rolling()) {
        float u = rollTime_ / kRollDuration;
        dir = rollDir_;
        speed = kRollSpeed * (1.0f - 0.5f * u);
        yaw = std::atan2(rollDir_.x, rollDir_.z);
        rollTime_ += dt;
        if (rollTime_ >= kRollDuration) {
            rollTime_ = -1;
            speed *= 0.5f;  // sale de la voltereta aún con algo de inercia
            counterWindow_ = 0.4f;  // un golpe ahora es un contraataque
        }
    }

    knock_ = knock_ * std::exp(-8.0f * dt);
    if (dashTime_ > 0) {
        dashTime_ -= dt;
        if (dashTime_ <= 0) dashVel_ = {};
    }
    // Paso adelante del golpe (avance propio de la animación).
    Vec3 stepVel;
    if (move_ != Move::None && body.grounded) {
        float dur = moveDuration(move_, attackSpeed);
        stepVel = forward() * stepSpeed(kMoves[int(move_)], moveU(), dur);
    }
    body.vel.x = dir.x * speed + knock_.x + dashVel_.x + stepVel.x;
    body.vel.z = dir.z * speed + knock_.z + dashVel_.z + stepVel.z;
    if (knock_.y > 0) {
        body.vel.y += knock_.y;
        knock_.y = 0;
    }
    if (jump && !busy()) {
        if (body.grounded) {
            body.vel.y = kJumpSpeed;
        } else if (!doubleJumpUsed_) {  // doble salto con voltereta
            body.vel.y = kJumpSpeed * 0.9f;
            doubleJumpUsed_ = !infiniteJumps;
            flipTime_ = 0;
        }
    }
    // Menos gravedad en el aire (forma Eco): se compensa parte antes de integrar.
    if (!body.grounded && gravityScale < 1.0f && move_ != Move::Plunge) body.vel.y += kGravity * (1.0f - gravityScale) * dt;
    // Picado: tras el saltito, cae a plomo.
    if (move_ == Move::Plunge && !impactDone_ && moveTime_ > 0.18f) body.vel.y = -22.0f;
    // Golpe aéreo: casi sin gravedad mientras dura (se queda flotando).
    if (isAirAttack(move_)) {
        body.vel.y += kGravity * kAirHover * dt;
        body.vel.y = std::max(body.vel.y, -1.5f);
    }

    if (!body.grounded) fallSpeed_ = std::max(fallSpeed_, -body.vel.y);
    moveBody(body, world, dt, kStepHeight);
    if (body.grounded && !isAirAttack(move_)) {
        airStep_ = 0;
        airComboSpent_ = false;
        doubleJumpUsed_ = false;
    }
    // Aterrizaje: se aplasta más cuanto más rápido caía.
    if (body.grounded && !wasGrounded_) {
        landStrength_ = clampf(fallSpeed_ / 10.0f, 0.15f, 1.0f);
        landTimer_ = 0.3f;
        fallSpeed_ = 0;
    }
    wasGrounded_ = body.grounded;
    landTimer_ = std::max(0.0f, landTimer_ - dt);

    // La fase avanza con la distancia recorrida (solo pisando suelo): los pies no patinan.
    if (body.grounded) phase_ = std::fmod(phase_ + speed * dt * 2.0f * kPi / 2.6f, 2.0f * kPi);
    airBlend_ = moveTowards(airBlend_, body.grounded ? 0.0f : 1.0f, 7.0f * dt);

    // Inclinación por aceleración y por giro, filtradas para que no tiemblen.
    float accel = (speed - prevSpeed_) / dt;
    float yawRate = wrapAngle(yaw - prevYaw_) / dt;
    prevSpeed_ = speed;
    prevYaw_ = yaw;
    if (rolling() || move_ != Move::None) accel = yawRate = 0;
    lean_ += (clampf(accel * 0.012f, -0.3f, 0.3f) - lean_) * (1.0f - std::exp(-10.0f * dt));
    float bankTarget = clampf(-yawRate * 0.05f * clampf(speed / kRunSpeed, 0, 1), -0.35f, 0.35f);
    bank_ += (bankTarget - bank_) * (1.0f - std::exp(-8.0f * dt));

    if (move_ != Move::None) {
        const MoveDef& d = kMoves[int(move_)];
        moveTime_ += dt;
        float u = moveTime_ / moveDuration(move_, attackSpeed);
        if (move_ == Move::Plunge) {
            // El picado golpea al tocar el suelo y se recupera enseguida.
            if (!impactDone_ && body.grounded && moveTime_ > 0.2f) {
                impactDone_ = true;
                pendingImpact_ = move_;
                moveTime_ = std::max(moveTime_, moveDuration(move_, attackSpeed) * 0.78f);
                u = moveTime_ / moveDuration(move_, attackSpeed);
            }
            if (!impactDone_) u = std::min(u, 0.75f);  // sigue arriba hasta aterrizar
        } else if (!impactDone_ && u >= d.impactU) {
            impactDone_ = true;
            pendingImpact_ = move_;
        }
        // Un golpe aéreo termina al tocar el suelo.
        if (isAirAttack(move_) && body.grounded && impactDone_) u = 1;
        // Un golpe en cola corta la recuperación del actual.
        bool chain = queued_ && isAttack(move_) && impactDone_ && u >= 0.55f;
        if (u >= 1.0f || chain) {
            if (isAttack(move_)) comboTimer_ = 0;
            move_ = Move::None;
            dashTime_ = 0;
            dashVel_ = {};
            if (queued_) {
                queued_ = false;
                Move next = nextAttack();
                if (next != Move::None) startMove(next, queuedYaw_, queuedDash_);
            }
        }
    } else {
        comboTimer_ += dt;
        if (comboTimer_ > kComboWindow) comboStep_ = 0;
    }
    guardTime_ = guarding() ? guardTime_ + dt : 0.0f;
    guardBlend_ = moveTowards(guardBlend_, guarding() ? 1.0f : 0.0f, 12.0f * dt);
    absorbBlend_ = moveTowards(absorbBlend_, absorbing() ? 1.0f : 0.0f, 6.0f * dt);

    // Pose: la del estado actual, fundida desde la anterior si el estado ha cambiado.
    int key = stateKey();
    if (key != lastKey_) {
        bool toAttack = isAttack(move_);
        bool fromAttack = lastKey_ >= 0 && isAttack(Move(lastKey_ & 31));
        blendDur_ = toAttack ? 0.05f : fromAttack ? 0.18f : rolling() ? 0.06f : 0.12f;
        blendFrom_ = pose;
        blendTime_ = lastKey_ < 0 ? blendDur_ : 0.0f;
        lastKey_ = key;
    }
    blendTime_ += dt;
    Pose target;
    animate(target);
    pose = blendTime_ >= blendDur_ ? target : lerpPose(blendFrom_, target, smoothstep01(blendTime_ / blendDur_), JointCount);
    computeWorldMatrices(pose, modelMatrix(), joints);
    updateSecondary(dt);
}

// Pelo: un punto con muelle que persigue a la cabeza (su retraso inclina los mechones).
// Cinta: cadena de Verlet colgada de la espalda, con gravedad, rozamiento y longitud fija.
void Character::updateSecondary(float dt) {
    Vec3 head = translationOf(joints[Head]);
    Vec3 anchor = transformPoint(joints[Chest], {0, 0.24f, -0.12f});
    const float seg = 0.11f;
    if (!secondaryInit_) {
        secondaryInit_ = true;
        hairPoint_ = head;
        for (int i = 0; i < kRibbonPoints; ++i) {
            ribbon_[i] = ribbonOld_[i] = ribbonPrevFrame_[i] = anchor - Vec3{0, seg * float(i), 0} - forward() * 0.02f * float(i);
        }
    }
    const float k = 160.0f, c = 14.0f;
    Vec3 acc = (head - hairPoint_) * k - hairVel_ * c;
    hairVel_ += acc * dt;
    hairPoint_ += hairVel_ * dt;
    hairOffset_ = hairPoint_ - head;
    float len = length(hairOffset_);
    if (len > 0.12f) {
        hairOffset_ = hairOffset_ * (0.12f / len);
        hairPoint_ = head + hairOffset_;
    }

    ribbon_[0] = anchor;
    Vec3 back = -forward();
    for (int i = 1; i < kRibbonPoints; ++i) {
        Vec3 v = (ribbon_[i] - ribbonOld_[i]) * 0.94f;
        ribbonOld_[i] = ribbon_[i];
        // Gravedad suave y un poco de "viento" hacia atrás: la cinta flota al correr.
        ribbon_[i] += v + (Vec3{0, -6.0f, 0} + back * 1.5f) * (dt * dt);
    }
    for (int it = 0; it < 4; ++it)
        for (int i = 1; i < kRibbonPoints; ++i) {
            Vec3 d = ribbon_[i] - ribbon_[i - 1];
            float l = length(d);
            if (l > 1e-5f) ribbon_[i] = ribbon_[i - 1] + d * (seg / l);
            // Que no atraviese el cuerpo: se mantiene detrás del plano de la espalda.
            Vec3 rel = ribbon_[i] - anchor;
            float f = dot(rel, forward());
            if (f > -0.02f) ribbon_[i] -= forward() * (f + 0.02f);
            ribbon_[i].y = std::max(ribbon_[i].y, body.pos.y + 0.03f);
        }
}

void Character::ribbon(float alpha, Vec3 out[kRibbonPoints]) const {
    for (int i = 0; i < kRibbonPoints; ++i) out[i] = ribbonPrevFrame_[i] + (ribbon_[i] - ribbonPrevFrame_[i]) * alpha;
}

void Character::interpolatedJoints(float alpha, Mat4 out[JointCount]) const {
    for (int i = 0; i < JointCount; ++i) out[i] = lerpMat(prevJoints_[i], joints[i], alpha);
}

// Recordatorio de signos (ver vmath.h): rotar +X lleva un miembro que cuelga hacia
// atrás (-Z); rotar +Z lo lleva hacia +X (la izquierda del personaje).
void Character::animate(Pose& out) const {
    out = Pose{};
    Vec3* r = out.rotation;

    float run = clampf(speed / kRunSpeed, 0, 1) * (1.0f - airBlend_);
    float sprint = clampf((speed - kRunSpeed) / (kSprintSpeed - kRunSpeed), 0, 1) * (1.0f - airBlend_);
    // Andando con la guardia (o cargando) se puede ir hacia atrás o de lado: el ciclo de
    // piernas se invierte hacia atrás y se abre de lado.
    float along = dot(moveDir_, forward());
    float back = along < -0.3f ? -1.0f : 1.0f;
    float s = std::sin(phase_) * back, c = std::cos(phase_);
    // Rebote: dos veces por ciclo (una por pisada), con el punto bajo al apoyar.
    float bounce = std::fabs(std::sin(phase_));

    // Carrera: piernas en oposición de fase, rodilla flexionada al adelantar la pierna.
    float legSwing = 0.85f * run, kneeBend = 1.6f * run;
    r[HipL].x = -legSwing * s - 0.1f * run;
    r[HipR].x = legSwing * s - 0.1f * run;
    r[KneeL].x = kneeBend * std::max(0.0f, c) + 0.15f * run + 0.05f;
    r[KneeR].x = kneeBend * std::max(0.0f, -c) + 0.15f * run + 0.05f;
    r[AnkleL].x = -0.5f * (r[HipL].x + r[KneeL].x) * run + 0.2f * run * std::max(0.0f, -s);
    r[AnkleR].x = -0.5f * (r[HipR].x + r[KneeR].x) * run + 0.2f * run * std::max(0.0f, s);
    r[HipL].z = 0.04f;
    r[HipR].z = -0.04f;

    // Brazo libre al contrario que su pierna; el del arma la lleva atrás al correr.
    r[ShoulderL].x = 0.9f * run * s;
    r[ShoulderL].z = 0.1f + 0.15f * run;
    r[ElbowL].x = -0.25f - 1.1f * run + 0.25f * run * s;
    r[ShoulderR].x = lerpf(-0.3f, 0.6f, run) - 0.25f * run * s;
    r[ShoulderR].z = -0.15f - 0.25f * run;
    r[ElbowR].x = lerpf(-1.3f, -0.5f, run);
    r[WristR].x = lerpf(0.75f, 0.9f, run);  // en reposo la hoja apunta al frente y abajo; corriendo, atrás

    // Torso: inclinado al correr (más al acelerar), ladeado en las curvas, con torsión
    // pelvis/pecho contrapuesta y la cabeza compensando para mirar al frente.
    r[Spine].x = 0.28f * run + lean_;
    r[Spine].z = bank_;
    r[Pelvis].z = -bank_ * 0.4f;
    r[Pelvis].y = 0.14f * s * run;
    r[Chest].y = -0.22f * s * run;
    r[Chest].x = 0.04f * run * bounce;
    r[Neck].x = -0.6f * r[Spine].x;
    r[Neck].z = -0.6f * bank_;
    r[Head].y = -(r[Pelvis].y + r[Chest].y);
    out.rootOffset.y = -0.06f * run * (1.0f - bounce) + 0.02f * run;
    // Corriendo a tope: más inclinado, zancada larga y brazos echados atrás.
    r[Spine].x += 0.25f * sprint;
    r[HipL].x *= 1.0f + 0.3f * sprint;
    r[HipR].x *= 1.0f + 0.3f * sprint;
    r[ShoulderL].x = lerpf(r[ShoulderL].x, 0.9f, 0.6f * sprint);
    r[ShoulderR].x = lerpf(r[ShoulderR].x, 1.0f, 0.6f * sprint);
    r[ElbowL].x -= 0.3f * sprint;

    // Reposo: postura de combate con las piernas abiertas y respiración.
    float idle = (1.0f - run) * (1.0f - airBlend_);
    float breath = std::sin(time_ * 2.2f);
    float sway = std::sin(time_ * 1.1f);
    r[HipL].z += 0.12f * idle;
    r[HipR].z -= 0.12f * idle;
    r[KneeL].x += 0.18f * idle;
    r[KneeR].x += 0.18f * idle;
    r[AnkleL].z -= 0.1f * idle;
    r[AnkleR].z += 0.1f * idle;
    out.rootOffset.y -= (0.035f + 0.01f * breath) * idle;
    out.rootOffset.x += 0.015f * sway * idle;
    r[Chest].x += 0.03f * breath * idle;
    r[Pelvis].y += 0.25f * idle;  // de medio lado, con el arma delante
    r[Chest].y -= 0.1f * idle;
    r[Head].y -= 0.15f * idle;
    r[Head].x += 0.03f * std::sin(time_ * 0.7f) * idle;
    r[ShoulderL].z += 0.04f * breath * idle;

    // En el aire: piernas recogidas (más al subir), brazos abiertos.
    float a = airBlend_;
    float rising = clampf(body.vel.y / kJumpSpeed, -1, 1);
    r[HipL].x = lerpf(r[HipL].x, -0.9f - 0.3f * rising, a);
    r[HipR].x = lerpf(r[HipR].x, -0.25f + 0.15f * rising, a);
    r[KneeL].x = lerpf(r[KneeL].x, 1.4f + 0.2f * rising, a);
    r[KneeR].x = lerpf(r[KneeR].x, 0.7f - 0.2f * rising, a);
    r[ShoulderL].z += 0.7f * a;
    r[ShoulderR].z -= 0.4f * a;
    r[Spine].x -= 0.1f * rising * a;

    // Aterrizaje: rodillas que absorben el golpe y vuelven con un pequeño rebote.
    if (landTimer_ > 0) {
        float t = 1.0f - landTimer_ / 0.3f;
        float w = std::sin(t * kPi) * (1.0f - t * 0.5f) * landStrength_;
        r[KneeL].x += 0.8f * w;
        r[KneeR].x += 0.8f * w;
        r[HipL].x -= 0.45f * w;
        r[HipR].x -= 0.45f * w;
        r[AnkleL].x -= 0.35f * w;
        r[AnkleR].x -= 0.35f * w;
        r[Spine].x += 0.25f * w;
        out.rootOffset.y -= 0.16f * w;
    }

    // Voltereta: cuerpo hecho una bola (el giro lo da modelMatrix()).
    if (rolling()) {
        float u = clampf(rollTime_ / kRollDuration, 0, 1);
        float w = clampf(std::min(u / 0.12f, (1.0f - u) / 0.2f), 0, 1);
        r[Spine].x = lerpf(r[Spine].x, 0.7f, w);
        r[Chest].x = lerpf(r[Chest].x, 0.4f, w);
        r[Neck].x = lerpf(r[Neck].x, 0.5f, w);
        for (Joint j : {HipL, HipR}) r[j].x = lerpf(r[j].x, -2.0f, w);
        for (Joint j : {KneeL, KneeR}) r[j].x = lerpf(r[j].x, 2.3f, w);
        for (Joint j : {ShoulderL, ShoulderR}) r[j].x = lerpf(r[j].x, -1.2f, w);
        for (Joint j : {ElbowL, ElbowR}) r[j].x = lerpf(r[j].x, -1.6f, w);
        out.rootOffset.y -= 0.35f * w;
    }

    // Movimiento en curso: brazo del arma por keyframes, tronco que acompaña el golpe
    // (se echa atrás al prepararlo y adelante al soltarlo) y piernas en posición.
    if (move_ != Move::None) {
        const MoveDef& d = kMoves[int(move_)];
        float u = moveU();
        float wIn = d.blendIn > 0 ? u / d.blendIn : 1.0f;
        float wOut = d.blendOut > 0 ? (1.0f - u) / d.blendOut : 1.0f;
        float w = clampf(std::min(wIn, wOut), 0, 1);
        ArmKey k = sampleKeys(d, u);
        applyArm(r, k, w, d.leftArm);
        if (moveSet == MoveSet::Fists && !d.leftArm && move_ != Move::Cast)  // guardia con el otro puño
            applyArm(r, {0, -0.6f, 0, -0.1f, -1.9f, 0, 0, Smooth}, w * 0.8f, true);
        float commit = clampf((u - d.impactU + 0.12f) / 0.12f, 0, 1);  // 0 preparando, 1 golpeado
        r[Spine].x += (d.lean * commit - 0.08f * (1.0f - commit)) * w;
        r[Spine].y += k.chestY * 0.35f * w;  // la cadera acompaña la torsión
        r[Head].y -= k.chestY * 0.5f * w;
        if (moveSet != MoveSet::Fists) {  // brazo libre equilibrando
            r[ShoulderL].x = lerpAngle(r[ShoulderL].x, -0.5f, w);
            r[ShoulderL].z = lerpAngle(r[ShoulderL].z, 0.9f, w);
            r[ElbowL].x = lerpAngle(r[ElbowL].x, -0.5f, w);
        }
        if (body.grounded) {
            float lunge = 0.15f + 0.25f * commit;  // zancada que se abre al golpear
            r[HipL].x = lerpAngle(r[HipL].x, -0.35f - lunge, w);  // pierna izquierda adelantada
            r[HipR].x = lerpAngle(r[HipR].x, 0.2f + lunge, w);
            r[KneeL].x = lerpAngle(r[KneeL].x, 0.5f + 0.2f * commit, w);
            r[KneeR].x = lerpAngle(r[KneeR].x, 0.25f, w);
            out.rootOffset.y -= (0.05f + 0.05f * commit) * w;
        }
    }

    // Cargando el corte: el arma sube poco a poco por encima de la cabeza y el cuerpo se
    // agacha y se echa atrás, tenso.
    if (float ch = charge(); ch > 0) {
        float w = smoothstep01(ch * 1.5f);
        applyArm(r, {0, -3.0f, 0.1f, -0.2f, -0.6f, 0.3f, -0.2f, Smooth}, w);
        r[Spine].x -= 0.12f * w;
        r[KneeL].x += 0.4f * w;
        r[KneeR].x += 0.4f * w;
        out.rootOffset.y -= 0.1f * w;
        if (ch >= 1.0f) r[Chest].y += 0.04f * std::sin(time_ * 40.0f);  // tiembla: listo
    }
    // Absorbiendo luz: brazos abiertos hacia arriba, cabeza alta, se eleva un poco.
    if (absorbBlend_ > 0) {
        float w = smoothstep01(absorbBlend_);
        float pulse = std::sin(time_ * 6.0f) * 0.05f;
        applyArm(r, {0, -2.2f, -0.6f, -0.9f, -0.3f, 0.4f, 0, Smooth}, w);
        applyArm(r, {0, -2.2f, -0.6f, -0.9f, -0.3f, 0.4f, 0, Smooth}, w, true);
        r[Spine].x -= (0.2f + pulse) * w;
        r[Neck].x -= 0.35f * w;
        out.rootOffset.y += (0.12f + pulse) * w;
    }
    // Guardia: hoja horizontal delante del cuerpo, agachado.
    if (guardBlend_ > 0) {
        float w = guardBlend_;
        applyArm(r, {0, -1.2f, 0.7f, 0.0f, -0.9f, 1.3f, -0.3f, Smooth}, w);
        r[ShoulderL].x = lerpAngle(r[ShoulderL].x, -1.0f, w);
        r[ShoulderL].y = lerpAngle(r[ShoulderL].y, -0.5f, w);
        r[ElbowL].x = lerpAngle(r[ElbowL].x, -1.0f, w);
        r[Spine].x += 0.15f * w;
        r[KneeL].x += 0.35f * w;
        r[KneeR].x += 0.35f * w;
        r[HipL].x -= 0.2f * w;
        r[HipR].x -= 0.2f * w;
        out.rootOffset.y -= 0.1f * w;
    }

    // Golpe recibido: echa el tronco y la cabeza atrás.
    if (hurtTimer_ > 0) {
        float w = std::sin(clampf(hurtTimer_ / 0.35f, 0, 1) * kPi);
        r[Spine].x -= 0.35f * w;
        r[Neck].x -= 0.3f * w;
        r[ShoulderL].z += 0.5f * w;
        r[ShoulderR].z -= 0.5f * w;
    }
    // Derrotado o tumbado: brazos y cabeza sueltos.
    if (downTimer >= 0 || lying > 0) {
        float w = downTimer >= 0 ? clampf(downTimer / 0.4f, 0, 1) : smoothstep01(lying);
        r[ShoulderL].z = lerpf(r[ShoulderL].z, 1.2f, w);
        r[ShoulderR].z = lerpf(r[ShoulderR].z, -1.2f, w);
        r[Neck].x = lerpf(r[Neck].x, -0.4f, w);
    }
}
