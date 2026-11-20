#include "actors/character.h"

#include "actors/hero_clips.h"

#include <algorithm>

namespace {

constexpr float kAcceleration = 26.0f;  // m/s²: arranca con algo de inercia (~0,25 s a la carrera)
constexpr float kDeceleration = 30.0f;  // y frena sin pararse en seco
constexpr float kTurnSmooth = 10.0f;    // giro amortiguado (más alto = más rápido)
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
const ArmKey kSweepKeys[] = {    // martillo: brazo abierto y bajo, el cuerpo da media vuelta
    {0.00f, GUARD, Smooth},
    {0.30f, -0.70f, -0.90f, -0.60f, -0.20f, 1.2f, -0.80f, Windup},
    {0.50f, -0.90f,  0.40f, -1.20f, -0.10f, 1.3f,  0.20f, Strike},
    {0.70f, -0.90f,  0.80f, -1.10f, -0.15f, 1.3f,  0.40f, Overshoot},
    {1.00f, GUARD, Smooth},
};
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
    {0.66f, 0.70f, 0.05f, 0.25f, kOverheadKeys, N(kOverheadKeys), 0,       0,        0.00f, 1.00f, 0.0f, 0.35f, 0.4f, 0.55f, 0.75f}, // Smash
    {0.62f, 0.50f, 0.05f, 0.25f, kSweepKeys,    N(kSweepKeys),  kPi,       0,        0.25f, 0.65f, 0.0f, 0.15f, 0.3f, 0.3f, 0.6f},  // Sweep
    {0.95f, 0.72f, 0.05f, 0.25f, kOverheadKeys, N(kOverheadKeys), 0,       0,        0.00f, 1.00f, 6.5f, 0.40f, 0.6f, 0.2f, 0.6f},  // Quake
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

// Movimiento en látigo (acción superpuesta): el pecho va un poco por delante del hombro, y el
// codo y la muñeca, un poco por detrás. Cada parte llega a la pose clave en un instante
// distinto y el golpe se desenrolla del tronco a la punta del arma.
ArmKey sampleOverlap(const MoveDef& d, float u, float duration) {
    // El desfase en segundos (unos 15 ms por articulación), pasado a fracción del golpe.
    float lag = clampf(0.016f / std::max(duration, 0.05f), 0.0f, 0.05f);
    ArmKey k = sampleKeys(d, u);
    ArmKey lead = sampleKeys(d, std::min(1.0f, u + lag));
    ArmKey elbow = sampleKeys(d, std::max(0.0f, u - lag));
    ArmKey wrist = sampleKeys(d, std::max(0.0f, u - 2.0f * lag));
    k.chestY = lead.chestY;
    k.elbowX = elbow.elbowX;
    k.wristX = wrist.wristX;
    return k;
}

// Curva suave en lugar de max(0, x): sin el pico en el cero (la rodilla no da tirones).
float softPositive(float x, float k = 0.12f) { return 0.5f * (x + std::sqrt(x * x + k * k)) - 0.5f * k; }

// Metros que avanza el cuerpo en un ciclo de pasos (dos pisadas): andando, zancadas cortas;
// corriendo, largas; a tope, aún más (sin que los pies se aceleren tanto). Con las animaciones
// hechas a mano, la carrera va al ritmo con que se hizo el clip (`runCycle`: un ciclo en 0,62 s
// a la velocidad de correr en el de Vela, 0,58 s en el de Hollín; a tope, un 11 % menos): con
// la zancada procedural iba 1,6 veces más deprisa y las piernas daban tirones.
float strideLength(float speed, float runCycle) {
    float run = clampf(speed / Character::kRunSpeed, 0, 1);
    float sprint = clampf((speed - Character::kRunSpeed) / (Character::kSprintSpeed - Character::kRunSpeed), 0, 1);
    if (runCycle > 0)
        return lerpf(1.5f, Character::kRunSpeed * runCycle, smoothstep01(run)) +
               (Character::kSprintSpeed * runCycle * (0.55f / 0.62f) - Character::kRunSpeed * runCycle) * sprint;
    return lerpf(1.5f, 2.5f, smoothstep01(run)) + 0.8f * sprint;
}

// Muelle de cada articulación: frecuencia (rad/s) y amortiguamiento (1 = sin rebote).
// Las piernas, firmes (los pies no patinan); la cabeza y los brazos, más sueltos.
struct JointSpring {
    float omega, zeta;
};
const JointSpring kSprings[JointCount] = {
    {32, 1.0f},   // Pelvis
    {22, 0.75f},  // Spine
    {20, 0.7f},   // Chest
    {17, 0.7f},   // Neck
    {15, 0.65f},  // Head
    {24, 0.7f}, {24, 0.65f}, {26, 0.6f},  // brazo izquierdo
    {24, 0.7f}, {24, 0.65f}, {26, 0.6f},  // brazo derecho
    {38, 1.0f}, {38, 1.0f}, {34, 1.0f}, {34, 1.0f},  // pierna izquierda
    {38, 1.0f}, {38, 1.0f}, {34, 1.0f}, {34, 1.0f},  // pierna derecha
};

// Paso implícito de un muelle amortiguado hacia `target` (estable con cualquier dt).
float springStep(float x, float& v, float target, float omega, float zeta, float dt, bool angle) {
    float diff = angle ? wrapAngle(target - x) : target - x;
    v = (v + omega * omega * dt * diff) / (1.0f + 2.0f * zeta * omega * dt + omega * omega * dt * dt);
    x += v * dt;
    return angle ? wrapAngle(x) : x;
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

// Animaciones hechas a mano (hero_clips.h). Los ciclos traen dos pasos completos: un paso
// (izquierda y derecha) dura la mitad del clip, y su fase es la del ciclo procedural.
constexpr float kRunCycle = 0.62f, kWalkCycle = 1.05f;
constexpr float kHollinRunCycle = 0.58f, kHollinWalkCycle = 1.2f;  // Hollín, más pesada y chulesca
// Duración del ciclo de carrera hecho a mano con que anda (0: sin clips, el procedural).
float locoRunCycle(bool hollin) {
    return hollin && hollinClipsLoaded() ? kHollinRunCycle : heroClipsLoaded() ? kRunCycle : 0.0f;
}
constexpr float kRollClipEnd = 0.9f;  // la voltereta acaba de pie en la guardia; luego solo espera

// Tramo de un clip que hace un golpe: empieza, instante en que el arma llega (el impacto del
// juego) y acaba.
struct ClipSegment {
    HeroClip clip;
    float from, impact, to;
};
// Los de la hoja salen del combo ×3 de Vela; los del martillo, de los de Hollín (el combo:
// barrido, golpe ascendente y martillazo al suelo; el salto con martillazo y el torbellino).
const ClipSegment* comboSegment(Move m, MoveSet set) {
    static const ClipSegment kTajo{HeroClip::Combo, 0.0f, 0.24f, 0.46f}, kReves{HeroClip::Combo, 0.46f, 0.53f, 0.74f},
        kAlSuelo{HeroClip::Combo, 0.74f, 1.06f, 1.6f};
    static const ClipSegment kBarrido{HeroClip::HammerCombo, 0.0f, 0.42f, 0.66f},
        kAscendente{HeroClip::HammerCombo, 0.66f, 0.98f, 1.12f}, kMartillazo{HeroClip::HammerCombo, 1.06f, 1.42f, 2.0f},
        kSaltoMartillazo{HeroClip::HammerLeap, 0.22f, 0.86f, 1.6f}, kPicado{HeroClip::HammerLeap, 0.46f, 0.86f, 1.6f},
        kTorbellino{HeroClip::HammerWhirl, 0.0f, 0.75f, 1.3f};
    if (set == MoveSet::Hammer && hollinClipsLoaded()) {
        switch (m) {
            case Move::Sweep: return &kBarrido;
            case Move::Smash: return &kMartillazo;
            case Move::Launcher: return &kAscendente;  // lanza hacia arriba
            case Move::Quake: return &kSaltoMartillazo;
            case Move::Plunge: return &kPicado;  // desde el aire: solo la caída
            case Move::Charged: return &kTorbellino;
            default: return nullptr;
        }
    }
    if (set != MoveSet::Blade || !heroClipsLoaded()) return nullptr;
    switch (m) {
        case Move::Slash1: return &kTajo;
        case Move::Slash2: return &kReves;
        case Move::Finisher: return &kAlSuelo;  // golpe descendente con salto (sin la voltereta procedural)
        default: return nullptr;
    }
}
// Instante del clip para el punto `u` (0..1) del golpe: el impacto del juego cae en el del clip.
float segmentTime(const ClipSegment& s, float u, float impactU) {
    if (u < impactU) return s.from + (s.impact - s.from) * (u / std::max(impactU, 1e-3f));
    return s.impact + (s.to - s.impact) * ((u - impactU) / std::max(1.0f - impactU, 1e-3f));
}

// Funde la pose de un clip sobre `out` con peso `w`; el brazo derecho (el del arma) con
// `armW` y la altura de la cadera con `rootW` (en el aire la pone la física, no el clip).
void mixClip(Pose& out, const Pose& clip, float w, float armW, float rootW) {
    for (int j = 0; j < JointCount; ++j) {
        float k = j == ShoulderR || j == ElbowR || j == WristR ? armW : w;
        if (k <= 0) continue;
        Vec3& r = out.rotation[j];
        const Vec3& c = clip.rotation[j];
        r = {lerpAngle(r.x, c.x, k), lerpAngle(r.y, c.y, k), lerpAngle(r.z, c.z, k)};
    }
    out.rootOffset.y = lerpf(out.rootOffset.y, clip.rootOffset.y, rootW);
}

}  // namespace

void Character::reset() { *this = Character{}; }

void Character::teleport(Vec3 pos, float newYaw) {
    body.pos = prevPos_ = pos;
    body.vel = {};
    knock_ = dashVel_ = moveVel_ = {};
    yaw = prevYaw_ = newYaw;
    speed = 0;
    secondaryInit_ = false;
    springInit_ = false;
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
        bool clip = comboSegment(move_, moveSet) != nullptr;  // el clip ya trae sus giros
        if (d.spin != 0 && !clip) m = m * rotateY(d.spin * t);
        if (d.flip != 0 && !clip) {  // voltereta alrededor del centro del cuerpo
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
        float turn = heroClipsLoaded() ? sampleHeroClip(HeroClip::Roll, u * kRollClipEnd).spin : smoothstep01(u) * 2.0f * kPi;
        m = m * translate(pivot) * rotateX(turn) * translate(-pivot);
    }
    return m;
}

Move Character::nextAttack() const {
    if (!body.grounded) {
        if (airComboSpent_) return Move::None;
        return airStep_ == 0 ? Move::Air1 : airStep_ == 1 ? Move::Air2 : Move::AirFinisher;
    }
    if (comboStep_ >= comboHits)
        return moveSet == MoveSet::Fists ? Move::Uppercut : moveSet == MoveSet::Hammer ? Move::Quake : Move::Finisher;
    switch (moveSet) {
        case MoveSet::Hammer: return comboStep_ % 2 ? Move::Sweep : Move::Smash;
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
    moveVel_ = {};
    const MoveDef& d = kMoves[int(m)];
    if (isAirAttack(m)) {
        ++airStep_;
        if (m == Move::AirFinisher) airComboSpent_ = true;
        body.vel.y = std::max(body.vel.y, 2.0f);  // un pequeño impulso: el combo aéreo "flota"
    } else if (m == Move::Finisher || m == Move::Uppercut || m == Move::Quake) {
        comboStep_ = 0;
    } else if (m == Move::Slash1 || m == Move::Slash2 || m == Move::Slash3 || m == Move::Thrust || m == Move::Punch ||
               m == Move::PunchL || m == Move::Smash || m == Move::Sweep) {
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
    if (rolling() || hurt() || downTimer >= 0 || lying > 0.01f || climbing()) return false;
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
    if (rolling() || hurt() || downTimer >= 0 || !body.grounded || climbing()) return false;
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

void Character::flinch(float seconds, Vec3 push) {
    if (climb_ == Climb::Ladder || climb_ == Climb::Wall) releaseClimb();  // un golpe la despega
    hurtTimer_ = seconds;
    // Sacudida: el tronco y la cabeza salen despedidos hacia donde empuja el golpe y los
    // muelles los devuelven con un rebote.
    Vec3 p = flat(push);
    if (length(p) > 0.01f) {
        p = normalize(p);
        Vec3 right{std::cos(yaw), 0, -std::sin(yaw)};  // +X del personaje
        float back = -dot(p, forward()), side = dot(p, right);
        springVel_[Spine].x -= 7.0f * back;
        springVel_[Neck].x -= 9.0f * back;
        springVel_[Spine].z -= 6.0f * side;
        springVel_[Head].z -= 8.0f * side;
        springVel_[ShoulderL].z += 6.0f;
        springVel_[ShoulderR].z -= 6.0f;
    }
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
           (hurt() ? 1 << 8 : 0) | (downTimer >= 0 ? 1 << 9 : 0) | (int(climb_) << 10);
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
    regrab_ = std::max(0.0f, regrab_ - dt);
    airTime_ = body.grounded ? 0.0f : airTime_ + dt;
    if (body.grounded) lastWallSet_ = false;  // pisando suelo, cualquier pared vale otra vez

    // Trepando (o agarrándose ahora): ni golpes ni carrera; la pose sale de animateClimb().
    if (climbing() || tryGrab(input, world)) {
        updateClimb(dt, input, jump, world);
        if (climbing()) {
            speed = 0;
            moveVel_ = {};
            airBlend_ = 0;
            prevSpeed_ = 0;
            prevYaw_ = yaw;
            finishPose(dt);
            return;
        }
    }
    // Tras el salto de pared, la marcha sigue un momento el impulso (no se vuelve a la pared).
    if (jumpLock_ > 0) {
        jumpLock_ -= dt;
        moveDir_ = forward();
        input = 1;
    }

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
    bool canMove = !busy();
    bool aimed = guarding() || charge() > 0;
    float maxSpeed = (aimed ? kGuardSpeed : (sprintHeld_ && body.grounded ? kSprintSpeed : kRunSpeed)) * speedScale;
    float targetSpeed = canMove ? maxSpeed * input : 0.0f;
    // Sin apuntar, el cuerpo corre hacia donde mira: gira hacia la marcha (más deprisa
    // cuanto más despacio va) y la velocidad sigue al cuerpo, así los pies nunca van de
    // lado. En las curvas cerradas afloja y en la media vuelta casi frena: pivota.
    // Apuntando (guardia, carga) mira a la cámara y se desplaza en cualquier dirección.
    if (aimed || !canMove) {
        Vec3 targetVel = (input > 0.01f ? moveDir_ : forward()) * targetSpeed;
        Vec3 diff = targetVel - moveVel_;
        bool speeding = length(targetVel) > length(moveVel_) && dot(targetVel, moveVel_) >= 0;
        float rate = speeding ? kAcceleration : kDeceleration;
        if (dot(targetVel, moveVel_) < 0) rate *= 1.6f;
        float dl = length(diff), maxDelta = rate * dt;
        moveVel_ += dl > maxDelta ? diff * (maxDelta / dl) : diff;
        if (canMove && aimed) {
            float step = wrapAngle(camYaw_ - yaw) * (1.0f - std::exp(-kTurnSmooth * dt));
            yaw = wrapAngle(yaw + clampf(step, -14.0f * dt, 14.0f * dt));
        }
    } else {
        float cur = length(moveVel_);
        if (input > 0.01f) {
            float off = wrapAngle(std::atan2(moveDir_.x, moveDir_.z) - yaw);
            float fast = clampf(cur / kSprintSpeed, 0, 1);
            float maxTurn = lerpf(16.0f, 9.0f, fast);
            float step = off * (1.0f - std::exp(-lerpf(18.0f, 11.0f, fast) * dt));
            yaw = wrapAngle(yaw + clampf(step, -maxTurn * dt, maxTurn * dt));
            float c = std::cos(wrapAngle(std::atan2(moveDir_.x, moveDir_.z) - yaw));
            targetSpeed *= clampf(0.6f + 0.4f * c, 0.15f, 1.0f);  // 90° → 60 %, media vuelta → 20 %
        }
        float rate = targetSpeed > cur ? kAcceleration : kDeceleration;
        cur = moveTowards(cur, targetSpeed, rate * dt);
        moveVel_ = forward() * cur;
    }
    speed = length(moveVel_);
    Vec3 dir = speed > 0.01f ? moveVel_ * (1.0f / speed) : forward();
    if (rolling()) {
        float u = rollTime_ / kRollDuration;
        dir = rollDir_;
        speed = kRollSpeed * (1.0f - 0.5f * u);
        moveVel_ = dir * speed;
        yaw = std::atan2(rollDir_.x, rollDir_.z);
        rollTime_ += dt;
        if (rollTime_ >= kRollDuration) {
            rollTime_ = -1;
            speed *= 0.5f;  // sale de la voltereta aún con algo de inercia
            moveVel_ = moveVel_ * 0.5f;
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
    if (carried) {
        body.pos = carryPos;
        body.vel = {};
        body.grounded = true;
    } else {
        moveBody(body, world, dt, kStepHeight);
    }
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

    // Tiempo sin hacer nada: el reposo se va relajando.
    bool still = speed < 0.3f && move_ == Move::None && !rolling() && body.grounded && !hurt() && !guarding();
    idleTime_ = still ? idleTime_ + dt : 0.0f;
    // La fase avanza con la distancia recorrida (solo pisando suelo): los pies no patinan.
    // Al girar casi parado da pasitos: la fase también avanza con el giro.
    float spin = speed < kRunSpeed ? std::fabs(wrapAngle(yaw - prevYaw_)) * 0.35f : 0.0f;
    if (body.grounded) phase_ = std::fmod(phase_ + (speed * dt + spin) * 2.0f * kPi / strideLength(speed, locoRunCycle(hollin)), 2.0f * kPi);
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
    float shuffle = rolling() || move_ != Move::None || !body.grounded ? 0.0f : clampf(std::fabs(yawRate) / 6.0f, 0, 1);
    turnStep_ += (shuffle - turnStep_) * (1.0f - std::exp(-12.0f * dt));

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
    finishPose(dt);
}

// La pose del estado actual, fundida desde la anterior si el estado ha cambiado, con sus
// muelles; luego las matrices y el movimiento secundario.
void Character::finishPose(float dt) {
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
    drawTime_ = std::min(kDrawTime, drawTime_ + dt);
    armedBlend_ = moveTowards(armedBlend_, weaponInHand() ? 1.0f : 0.0f, dt / 0.2f);
    alertBlend_ = moveTowards(alertBlend_, alert_ || move_ != Move::None ? 1.0f : 0.0f, dt / 0.35f);
    Pose target;
    animate(target);
    pose = blendTime_ >= blendDur_ ? target : lerpPose(blendFrom_, target, smoothstep01(blendTime_ / blendDur_), JointCount);
    applySprings(dt);
    // Animación limitada en los golpes: poses mantenidas a 20 fps.
    if (kLimitedAnimation && (isAttack(move_) || move_ == Move::Cast)) {
        holdTimer_ -= dt;
        if (!stepping_ || holdTimer_ <= 0) {
            heldPose_ = pose;
            holdTimer_ = 1.0f / 20.0f;
        }
        pose = heldPose_;
        stepping_ = true;
    } else {
        stepping_ = false;
    }
    computeWorldMatrices(pose, modelMatrix(), joints);
    updateSecondary(dt);
}

// La pose mostrada persigue a la calculada con un muelle por articulación. Golpeando, el brazo
// del arma va más rápido (y con algo más de rebote: el arma se pasa y vuelve); en la voltereta,
// tumbado o derrotado, todo va firme para que el cuerpo no se deforme.
void Character::applySprings(float dt) {
    if (!springInit_ || dt <= 0) {
        springInit_ = true;
        sprung_ = pose;
        for (Vec3& v : springVel_) v = {};
        rootSpringVel_ = {};
        return;
    }
    bool striking = isAttack(move_);
    bool firm = rolling() || downTimer >= 0 || lying > 0.01f || flipTime_ >= 0;
    const MoveDef& d = kMoves[int(move_)];
    Joint sh = d.leftArm ? ShoulderL : ShoulderR, el = d.leftArm ? ElbowL : ElbowR, wr = d.leftArm ? WristL : WristR;
    for (int j = 0; j < JointCount; ++j) {
        float omega = kSprings[j].omega, zeta = kSprings[j].zeta;
        if (striking && (j == sh || j == el || j == wr)) omega *= 1.7f, zeta *= 0.85f;
        else if (striking && (j == Spine || j == Chest)) omega *= 1.3f;
        if (firm) omega = std::max(omega, 45.0f), zeta = 1.0f;
        Vec3& x = sprung_.rotation[j];
        Vec3& v = springVel_[j];
        const Vec3& t = pose.rotation[j];
        x.x = springStep(x.x, v.x, t.x, omega, zeta, dt, true);
        x.y = springStep(x.y, v.y, t.y, omega, zeta, dt, true);
        x.z = springStep(x.z, v.z, t.z, omega, zeta, dt, true);
    }
    Vec3& ro = sprung_.rootOffset;
    float ow = firm ? 45.0f : 30.0f;
    ro.x = springStep(ro.x, rootSpringVel_.x, pose.rootOffset.x, ow, 0.9f, dt, false);
    ro.y = springStep(ro.y, rootSpringVel_.y, pose.rootOffset.y, ow, 0.9f, dt, false);
    ro.z = springStep(ro.z, rootSpringVel_.z, pose.rootOffset.z, ow, 0.9f, dt, false);
    pose = sprung_;
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
    if (stepping_) {  // pose mantenida: sin mezclar poses, solo se desplaza el cuerpo
        Mat4 shift = translate(interpolatedPos(alpha) - body.pos);
        for (int i = 0; i < JointCount; ++i) out[i] = shift * joints[i];
        return;
    }
    for (int i = 0; i < JointCount; ++i) out[i] = lerpMat(prevJoints_[i], joints[i], alpha);
}

// Recordatorio de signos (ver vmath.h): rotar +X lleva un miembro que cuelga hacia
// atrás (-Z); rotar +Z lo lleva hacia +X (la izquierda del personaje).
void Character::animate(Pose& out) const {
    out = Pose{};
    if (climbing()) {
        animateClimb(out);
        return;
    }
    Vec3* r = out.rotation;

    float run = clampf(speed / kRunSpeed, 0, 1) * (1.0f - airBlend_);
    float sprint = clampf((speed - kRunSpeed) / (kSprintSpeed - kRunSpeed), 0, 1) * (1.0f - airBlend_);
    // Andar o correr: por debajo de ~3 m/s anda (rodillas casi rectas, sin fase de vuelo,
    // brazos sueltos y el tronco derecho); por encima, corre.
    float gait = smoothstep01((speed - 2.0f) / 2.5f);
    // Andando con la guardia (o cargando) se puede ir hacia atrás o de lado: el ciclo de
    // piernas se invierte hacia atrás y se abre de lado.
    float along = dot(moveDir_, forward());
    float back = along < -0.3f ? -1.0f : 1.0f;
    float s = std::sin(phase_) * back, c = std::cos(phase_);
    // Rebote: dos veces por ciclo (una por pisada), suave, con el punto bajo justo después de
    // apoyar el pie (el cuerpo cae sobre la pierna y luego se impulsa).
    float bounce = 0.5f - 0.5f * std::cos(2.0f * phase_ - 0.5f);

    // Piernas en oposición de fase. La rodilla se dobla al pasar la pierna hacia delante
    // (mucho más corriendo: el talón sube hacia el glúteo) y se estira al apoyar; el tobillo
    // apunta la punta al despegar y la levanta al recibir el suelo con el talón.
    // Girando en el sitio: pasitos cortos (levanta un poco las rodillas por turnos).
    float shuffle = turnStep_ * (1.0f - run) * (1.0f - airBlend_);
    float legSwing = lerpf(0.5f, 0.85f, gait) * run + 0.25f * shuffle;
    float kneeBend = lerpf(0.7f, 1.7f, gait) * run + 0.7f * shuffle;
    float sL = std::sin(phase_ + 0.35f) * back, sR = -sL;  // la rodilla va algo por delante de la cadera
    r[HipL].x = -legSwing * s - 0.1f * run;
    r[HipR].x = legSwing * s - 0.1f * run;
    r[KneeL].x = kneeBend * softPositive(c) + lerpf(0.06f, 0.18f, gait) * run + 0.05f;
    r[KneeR].x = kneeBend * softPositive(-c) + lerpf(0.06f, 0.18f, gait) * run + 0.05f;
    r[AnkleL].x = (-0.45f * (r[HipL].x + r[KneeL].x) + 0.35f * softPositive(-sL) - 0.15f * softPositive(sL)) * run;
    r[AnkleR].x = (-0.45f * (r[HipR].x + r[KneeR].x) + 0.35f * softPositive(-sR) - 0.15f * softPositive(sR)) * run;
    r[ToeL].x = -0.4f * softPositive(-s) * run;  // la punta se dobla al despegar
    r[ToeR].x = -0.4f * softPositive(s) * run;
    r[HipL].z = 0.04f;
    r[HipR].z = -0.04f;

    // Brazos al contrario que su pierna, un pelín retrasados (siguen al tronco, no lo
    // mueven); el codo se dobla más al ir hacia delante. El del arma la lleva atrás al correr.
    float sa = std::sin(phase_ - 0.25f) * back;
    float armSwing = lerpf(0.45f, 0.9f, gait) * run;
    r[ShoulderL].x = armSwing * sa;
    r[ShoulderL].z = 0.1f + 0.15f * run;
    r[ElbowL].x = -0.25f - lerpf(0.35f, 1.1f, gait) * run - 0.3f * run * softPositive(-sa);
    r[ShoulderR].x = lerpf(-0.3f, 0.6f, run) - 0.25f * run * sa;
    r[ShoulderR].z = -0.15f - 0.25f * run;
    r[ElbowR].x = lerpf(-1.3f, -0.5f, run);
    r[WristR].x = lerpf(0.75f, 0.9f, run);  // en reposo la hoja apunta al frente y abajo; corriendo, atrás

    // Sin el arma en la mano (explorando): el brazo derecho se balancea como el izquierdo.
    float unarmed = 1.0f - armedBlend_;
    r[ShoulderR].x = lerpf(r[ShoulderR].x, -armSwing * sa, unarmed);
    r[ShoulderR].z = lerpf(r[ShoulderR].z, -0.1f - 0.15f * run, unarmed);
    r[ElbowR].x = lerpf(r[ElbowR].x, -0.25f - lerpf(0.35f, 1.1f, gait) * run - 0.3f * run * softPositive(sa), unarmed);
    r[WristR].x = lerpf(r[WristR].x, 0.0f, unarmed);

    // Torso: inclinado al correr (más al acelerar), ladeado en las curvas, con torsión
    // pelvis/pecho contrapuesta y la cabeza compensando para mirar al frente. La cadera cae
    // hacia el lado de la pierna que se levanta y se desplaza sobre la que apoya.
    r[Spine].x = lerpf(0.06f, 0.28f, gait) * run + lean_;
    r[Spine].z = bank_;
    r[Pelvis].z = -bank_ * 0.4f + 0.06f * s * run;
    r[Spine].z -= 0.04f * s * run;  // el tronco compensa la caída de la cadera
    r[Pelvis].y = lerpf(0.1f, 0.16f, gait) * s * run;
    r[Chest].y = -lerpf(0.12f, 0.24f, gait) * sa * run;
    r[Chest].x = 0.04f * run * bounce;
    r[Neck].x = -0.6f * r[Spine].x - 0.04f * run * bounce;
    r[Neck].z = -0.6f * bank_ - 0.5f * r[Pelvis].z;
    r[Head].y = -(r[Pelvis].y + r[Chest].y);
    out.rootOffset.y = -lerpf(0.025f, 0.065f, gait) * run * (1.0f - bounce) + 0.02f * run;
    out.rootOffset.x = 0.02f * s * run * (1.0f - gait);  // andando, el peso pasa de un pie a otro
    // Corriendo a tope: más inclinado, zancada larga y brazos echados atrás.
    r[Spine].x += 0.25f * sprint;
    r[HipL].x *= 1.0f + 0.3f * sprint;
    r[HipR].x *= 1.0f + 0.3f * sprint;
    r[ShoulderL].x = lerpf(r[ShoulderL].x, 0.9f, 0.6f * sprint);
    r[ShoulderR].x = lerpf(r[ShoulderR].x, 1.0f, 0.6f * sprint);
    r[ElbowL].x -= 0.3f * sprint;

    // Reposo: postura de combate con las piernas abiertas, que nunca está quieta del todo:
    // respira (pecho y hombros), pasa el peso de una pierna a otra, mira a los lados de vez
    // en cuando y, si sigue un rato sin hacer nada, se relaja (baja el arma, se encorva un
    // poco y a veces rueda los hombros).
    float idle = (1.0f - run) * (1.0f - airBlend_);
    float breath = std::sin(time_ * 1.7f);
    float weight = std::sin(time_ * 0.42f);  // peso de una pierna a otra (~15 s el ciclo)
    float relax = smoothstep01((idleTime_ - 4.0f) / 1.5f) * idle * (1.0f - armedBlend_);
    // Con el arma en la mano: guardia de combate, agachado y con un rebote ligero (listo).
    // Guardia de combate: de lado, pierna izquierda adelantada, rodillas flexionadas, la hoja
    // levantada en diagonal hacia delante y la mano libre adelantada; rebota, alerta.
    float ready = armedBlend_ * idle * (guarding() ? 0.0f : 1.0f) * alertBlend_;
    // Sin peligro cerca: de pie, rodillas sueltas, la hoja baja junto a la pierna apuntando
    // al suelo por delante y el otro brazo colgando.
    float calm = armedBlend_ * idle * (guarding() ? 0.0f : 1.0f) * (1.0f - alertBlend_);
    float hop = std::sin(time_ * 6.5f);
    out.rootOffset.y -= (0.07f + 0.018f * hop) * ready;
    r[Pelvis].y += 0.3f * ready;
    r[HipL].x -= 0.4f * ready;
    r[HipR].x += 0.3f * ready;
    r[KneeL].x += 0.3f * ready;
    r[KneeR].x += 0.45f * ready;
    r[AnkleL].x += 0.1f * ready;
    r[AnkleR].x -= 0.15f * ready;
    r[Spine].x += 0.12f * ready;
    r[Chest].y -= 0.2f * ready;
    r[Head].y -= 0.1f * ready;
    r[ShoulderR].x = lerpf(r[ShoulderR].x, -1.0f, ready);
    r[ShoulderR].z = lerpf(r[ShoulderR].z, -0.45f, ready);
    r[ElbowR].x = lerpf(r[ElbowR].x, -1.0f + 0.05f * hop, ready);
    r[WristR].x = lerpf(r[WristR].x, 0.1f, ready);
    r[ShoulderL].x = lerpf(r[ShoulderL].x, -0.8f, ready);
    r[ShoulderL].z = lerpf(r[ShoulderL].z, 0.25f, ready);
    r[ElbowL].x = lerpf(r[ElbowL].x, -0.9f, ready);
    r[ShoulderR].x = lerpf(r[ShoulderR].x, 0.05f, calm);
    r[ShoulderR].z = lerpf(r[ShoulderR].z, -0.18f, calm);
    r[ElbowR].x = lerpf(r[ElbowR].x, -0.45f + 0.03f * breath, calm);
    r[WristR].x = lerpf(r[WristR].x, 0.85f, calm);
    r[ShoulderL].x = lerpf(r[ShoulderL].x, 0.08f, calm);
    r[ShoulderL].z = lerpf(r[ShoulderL].z, 0.12f, calm);
    r[ElbowL].x = lerpf(r[ElbowL].x, -0.25f, calm);
    r[HipL].z += (0.12f - 0.04f * relax) * idle;
    r[HipR].z -= (0.12f - 0.04f * relax) * idle;
    r[KneeL].x += (0.18f + 0.14f * std::max(0.0f, -weight)) * idle;  // se dobla la rodilla que descansa
    r[KneeR].x += (0.18f + 0.14f * std::max(0.0f, weight)) * idle;
    r[AnkleL].z -= 0.1f * idle;
    r[AnkleR].z += 0.1f * idle;
    out.rootOffset.y -= (0.035f + 0.012f * breath + 0.02f * std::fabs(weight)) * idle;
    out.rootOffset.x += 0.035f * weight * idle;
    r[Pelvis].z += 0.07f * weight * idle;
    r[Spine].z -= 0.05f * weight * idle;
    r[Chest].x += (0.045f * breath + 0.12f * relax) * idle;
    r[Spine].x += 0.06f * relax;
    r[Pelvis].y += 0.25f * (1.0f - 0.5f * relax) * idle;  // de medio lado, con el arma delante
    r[Chest].y -= 0.1f * idle;
    r[ShoulderL].z += 0.05f * breath * idle;
    r[ShoulderR].z -= 0.05f * breath * idle;
    // Mirar alrededor: cada 7 s gira la cabeza a un lado (alterno) y vuelve.
    float gp = std::fmod(time_ + 3.0f, 7.0f);
    float glance = gp < 2.4f ? std::sin(gp / 2.4f * kPi) : 0.0f;
    float side = std::fmod(std::floor((time_ + 3.0f) / 7.0f), 2.0f) < 1.0f ? 1.0f : -1.0f;
    r[Head].y += (-0.15f + 0.6f * glance * side) * idle;
    r[Neck].y += 0.2f * glance * side * idle;
    r[Chest].y += 0.08f * glance * side * idle;
    r[Head].x += (0.03f * std::sin(time_ * 0.7f) + 0.1f * relax) * idle;
    // Relajado: baja el arma y deja caer el otro brazo.
    r[ShoulderR].x = lerpf(r[ShoulderR].x, 0.15f, 0.7f * relax);
    r[ElbowR].x = lerpf(r[ElbowR].x, -0.45f, 0.7f * relax);
    r[WristR].x = lerpf(r[WristR].x, 0.3f, 0.7f * relax);
    r[ElbowL].x = lerpf(r[ElbowL].x, -0.15f, 0.7f * relax);
    // Y cada 12 s rueda los hombros, como quien se estira.
    float rp = std::fmod(idleTime_, 12.0f);
    float roll = idleTime_ > 6.0f && rp < 1.6f ? std::sin(rp / 1.6f * kPi) : 0.0f;
    r[ShoulderL].x -= 0.35f * roll * std::sin(rp * 4.0f);
    r[ShoulderR].x -= 0.35f * roll * std::sin(rp * 4.0f + 1.0f);
    r[ShoulderL].z += 0.12f * roll;
    r[ShoulderR].z -= 0.12f * roll;
    r[Neck].x -= 0.25f * roll;

    // Andar y correr hechos a mano, en cuanto se mueve (parado, el reposo procedural). La fase
    // es la de las pisadas, así los pies no patinan; andando hacia atrás, el ciclo al revés. El
    // brazo del arma: con ella en la mano, el procedural (la lleva atrás al correr); andando,
    // siempre (el clip la apoya en el hombro).
    // Hollín, con los suyos si están.
    bool clips = heroClipsLoaded();
    if (float w = locoRunCycle(hollin) > 0 ? smoothstep01(speed / 1.2f) * (1.0f - airBlend_) : 0.0f; w > 0) {
        float cycle = back * phase_ / (2.0f * kPi);
        bool own = hollin && hollinClipsLoaded();
        Pose loco = own ? lerpPose(sampleHeroClip(HeroClip::HollinWalk, cycle * kHollinWalkCycle).pose,
                                   sampleHeroClip(HeroClip::HollinRun, cycle * kHollinRunCycle).pose, gait, JointCount)
                        : lerpPose(sampleHeroClip(HeroClip::Walk, cycle * kWalkCycle).pose,
                                   sampleHeroClip(HeroClip::Run, cycle * kRunCycle).pose, gait, JointCount);
        mixClip(out, loco, w, w * gait * (1.0f - armedBlend_), w);
        r[Spine].x += (lean_ + 0.15f * sprint) * w;  // se inclina al acelerar y al girar
        r[Spine].z += bank_ * w;
    }

    // Gesto de desenvainar o envainar: la mano derecha va al hombro (donde está la empuñadura).
    if (drawTime_ < kDrawTime) {
        float w = std::sin(drawTime_ / kDrawTime * kPi);
        r[ShoulderR].x = lerpf(r[ShoulderR].x, -2.7f, w);
        r[ShoulderR].z = lerpf(r[ShoulderR].z, -0.3f, w);
        r[ElbowR].x = lerpf(r[ElbowR].x, -1.9f, w);
        r[Chest].y += 0.25f * w;
    }

    // En el aire: piernas recogidas (más al subir), brazos abiertos.
    // Con el salto hecho a mano: al subir, brazos arriba; en lo alto, piernas recogidas; al
    // caer, se estira esperando el suelo (0,40 → 0,62 → 0,84 s del clip según la velocidad).
    float a = airBlend_;
    float rising = clampf(body.vel.y / kJumpSpeed, -1, 1);
    if (clips) {
        if (a > 0) mixClip(out, sampleHeroClip(HeroClip::Jump, 0.62f - 0.22f * rising).pose, a, a * (1.0f - armedBlend_), 0);
    } else {
        r[HipL].x = lerpf(r[HipL].x, -0.9f - 0.3f * rising, a);
        r[HipR].x = lerpf(r[HipR].x, -0.25f + 0.15f * rising, a);
        r[KneeL].x = lerpf(r[KneeL].x, 1.4f + 0.2f * rising, a);
        r[KneeR].x = lerpf(r[KneeR].x, 0.7f - 0.2f * rising, a);
        r[ShoulderL].z += 0.7f * a;
        r[ShoulderR].z -= 0.4f * a;
        r[Spine].x -= 0.1f * rising * a;
    }

    // Aterrizaje: rodillas que absorben el golpe y vuelven con un pequeño rebote. Con el clip,
    // su caída (se agacha y se levanta); corriendo, solo en parte, para no frenar las piernas.
    if (landTimer_ > 0 && clips) {
        float k = 1.0f - landTimer_ / 0.3f;
        float w = std::min(1.0f, 0.4f + landStrength_) * (1.0f - smoothstep01((k - 0.5f) / 0.5f)) *
                  (1.0f - 0.6f * clampf(speed / kRunSpeed, 0, 1));
        mixClip(out, sampleHeroClip(HeroClip::Jump, 0.92f + 0.38f * k).pose, w, w * (1.0f - armedBlend_), w);
    } else if (landTimer_ > 0) {
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
    if (rolling() && clips) {  // la hecha a mano (la vuelta, en modelMatrix())
        float u = clampf(rollTime_ / kRollDuration, 0, 1);
        mixClip(out, sampleHeroClip(HeroClip::Roll, u * kRollClipEnd).pose, 1, 1, 1);
    } else if (rolling()) {
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
        if (const ClipSegment* seg = comboSegment(move_, moveSet)) {  // golpe hecho a mano: todo el cuerpo
            // La altura de la cadera, solo en el suelo: en el aire (el salto del martillazo) la
            // pone la física.
            float rootW = seg->clip == HeroClip::Combo || body.grounded ? w : 0.0f;
            mixClip(out, sampleHeroClip(seg->clip, segmentTime(*seg, u, d.impactU)).pose, w, w, rootW);
        } else {
            ArmKey k = sampleOverlap(d, u, moveDuration(move_, attackSpeed));
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
