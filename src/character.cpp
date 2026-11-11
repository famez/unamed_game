#include "character.h"

#include <algorithm>

namespace {

constexpr float kAcceleration = 40.0f;  // m/s²: respuesta casi instantánea
constexpr float kTurnRate = 14.0f;      // rad/s
constexpr float kAimTurnRate = 30.0f;   // rad/s: al apuntar sigue a la cámara sin retraso visible

// Pose del brazo derecho (y giro del pecho) en un instante `u` ∈ [0,1] de la acción.
struct ArmKey {
    float u;
    float shoulderX, shoulderY, shoulderZ, elbowX, wristX, chestY;
};

struct ActionDef {
    float duration;   // segundos
    float impactU;    // momento en que el golpe/disparo/lanzamiento tiene efecto
    float blendIn, blendOut;
    const ArmKey* keys;
    int keyCount;
    bool guardOther;  // levanta el otro brazo en guardia
    bool leftArm;     // los keyframes se aplican, en espejo, al brazo izquierdo
};

// Recordatorio de signos: rotar +X lleva hacia atrás un brazo que cuelga; -X hacia
// delante. -1,57 = brazo horizontal al frente.
// clang-format off
const ArmKey kPunchKeys[] = {
    //  u      hombroX hombroY hombroZ codoX  muñecaX pechoY
    {0.00f,   -0.30f,  0.0f,  -0.10f, -1.80f, 0.0f,  0.00f},  // puño recogido
    {0.40f,   -1.55f,  0.0f,  -0.05f, -0.05f, 0.0f,  0.35f},  // brazo extendido
    {1.00f,   -0.30f,  0.0f,  -0.10f, -1.80f, 0.0f,  0.00f},
};
const ArmKey kUppercutKeys[] = {
    {0.00f,   -0.30f,  0.0f,  -0.10f, -1.80f, 0.0f,  0.00f},
    {0.35f,    0.35f,  0.0f,  -0.15f, -1.90f, 0.0f, -0.35f},  // puño bajo y atrás
    {0.50f,   -2.20f,  0.25f, -0.05f, -1.30f, 0.0f,  0.45f},  // gancho hacia arriba
    {1.00f,   -0.30f,  0.0f,  -0.10f, -1.80f, 0.0f,  0.00f},
};
const ArmKey kSlashKeys[] = {
    {0.00f,   -0.30f,  0.0f,  -0.10f, -1.30f, 0.0f,  0.00f},  // guardia, espada vertical
    {0.35f,   -2.90f,  0.3f,  -0.30f, -0.40f, 0.0f, -0.30f},  // espada levantada atrás
    {0.60f,   -0.40f, -0.4f,  -0.10f, -0.10f, 1.0f,  0.50f},  // tajo hacia abajo
    {1.00f,   -0.30f,  0.0f,  -0.10f, -1.30f, 0.0f,  0.00f},
};
// Barrido de derecha a izquierda con la hoja en la prolongación del brazo (muñeca +1,3).
const ArmKey kSweepKeys[] = {
    {0.00f,   -0.30f,  0.0f,  -0.10f, -1.30f, 0.0f,  0.00f},
    {0.30f,   -1.30f, -1.30f,  0.00f, -0.30f, 1.3f, -0.50f},  // espada abierta a la derecha
    {0.55f,   -1.40f,  0.90f,  0.00f, -0.20f, 1.3f,  0.60f},  // cruza por delante
    {1.00f,   -0.30f,  0.0f,  -0.10f, -1.30f, 0.0f,  0.00f},
};
const ArmKey kThrustKeys[] = {
    {0.00f,   -0.30f,  0.0f,  -0.10f, -1.30f, 0.0f,  0.00f},
    {0.35f,   -0.60f,  0.1f,  -0.20f, -1.80f, 1.3f, -0.40f},  // brazo recogido, punta al frente
    {0.50f,   -1.55f,  0.0f,   0.00f,  0.00f, 1.45f, 0.50f},  // estocada: brazo y hoja rectos
    {1.00f,   -0.30f,  0.0f,  -0.10f, -1.30f, 0.0f,  0.00f},
};
const ArmKey kShootKeys[] = {
    {0.00f,   -1.55f,  0.15f,  0.00f,  0.00f, 0.0f,  0.25f},  // apuntando
    {0.15f,   -1.80f,  0.15f,  0.00f, -0.20f, 0.0f,  0.25f},  // retroceso
    {1.00f,   -1.55f,  0.15f,  0.00f,  0.00f, 0.0f,  0.25f},
};
const ArmKey kShotgunKeys[] = {
    {0.00f,   -1.55f,  0.15f,  0.00f,  0.00f, 0.0f,  0.25f},
    {0.10f,   -2.15f,  0.15f,  0.00f, -0.45f, 0.0f,  0.10f},  // culatazo fuerte
    {0.45f,   -1.55f,  0.15f,  0.00f,  0.00f, 0.0f,  0.25f},
    {1.00f,   -1.55f,  0.15f,  0.00f,  0.00f, 0.0f,  0.25f},  // (amartillando)
};
const ArmKey kSmashKeys[] = {
    {0.00f,   -0.30f,  0.0f,  -0.10f, -1.30f, 0.0f,  0.00f},
    {0.45f,   -3.05f,  0.1f,  -0.20f, -0.70f, 0.0f, -0.25f},  // martillo por encima de la cabeza
    {0.62f,   -0.55f,  0.0f,  -0.10f,  0.00f, 1.1f,  0.30f},  // golpe contra el suelo
    {0.80f,   -0.60f,  0.0f,  -0.10f,  0.00f, 1.1f,  0.30f},
    {1.00f,   -0.30f,  0.0f,  -0.10f, -1.30f, 0.0f,  0.00f},
};
const ArmKey kThrowKeys[] = {
    {0.00f,   -0.20f,  0.0f,  -0.10f, -0.50f, 0.0f,  0.00f},
    {0.40f,    2.50f,  0.0f,  -0.20f, -1.30f, 0.0f, -0.40f},  // brazo arriba y atrás
    {0.55f,    4.30f,  0.0f,  -0.10f, -0.10f, 0.0f,  0.40f},  // suelta la piedra
    {1.00f,    6.08f,  0.0f,  -0.10f, -0.30f, 0.0f,  0.00f},  // acompaña hacia abajo
};

const ActionDef kActions[] = {
    // duración impacto entrada salida  keys             guardia izq.
    {0.00f,    0.00f,  0.00f,  0.00f,  nullptr,      0, false, false},  // None
    {0.30f,    0.40f,  0.10f,  0.25f,  kPunchKeys,   3, true,  false},  // Punch
    {0.50f,    0.50f,  0.05f,  0.20f,  kSlashKeys,   4, false, false},  // Slash
    {0.18f,    0.00f,  0.00f,  0.00f,  kShootKeys,   3, false, false},  // Shoot
    {0.55f,    0.55f,  0.10f,  0.20f,  kThrowKeys,   4, false, false},  // Throw
    {0.30f,    0.40f,  0.10f,  0.25f,  kPunchKeys,   3, true,  true},   // Punch2 (izquierdo)
    {0.45f,    0.50f,  0.10f,  0.20f,  kUppercutKeys, 4, true, false},  // Punch3 (gancho)
    {0.45f,    0.55f,  0.05f,  0.20f,  kSweepKeys,   4, false, false},  // Slash2 (barrido)
    {0.55f,    0.50f,  0.05f,  0.20f,  kThrustKeys,  4, false, false},  // Slash3 (estocada)
    {0.80f,    0.00f,  0.00f,  0.00f,  kShotgunKeys, 4, false, false},  // ShootHeavy
    {0.85f,    0.62f,  0.08f,  0.20f,  kSmashKeys,   5, true,  false},  // Smash
};

bool isGun(Action a) { return a == Action::Shoot || a == Action::ShootHeavy; }

// Cadena de cada arma: tras el tercer golpe se vuelve a empezar. El martillo no hace combo.
Action comboNext(Item weapon, Action last) {
    if (weapon == Item::Hammer) return Action::Smash;
    if (weapon == Item::Fists) return last == Action::Punch ? Action::Punch2 : last == Action::Punch2 ? Action::Punch3 : Action::Punch;
    return last == Action::Slash ? Action::Slash2 : last == Action::Slash2 ? Action::Slash3 : Action::Slash;
}
bool isMelee(Action a) {
    return a == Action::Punch || a == Action::Punch2 || a == Action::Punch3 || a == Action::Slash ||
           a == Action::Slash2 || a == Action::Slash3 || a == Action::Smash;
}
Item meleeWeapon(Action a) {
    if (a == Action::Smash) return Item::Hammer;
    if (a == Action::Slash || a == Action::Slash2 || a == Action::Slash3) return Item::Sword;
    return Item::Fists;
}
// clang-format on

ArmKey lerpKey(const ArmKey& a, const ArmKey& b, float t) {
    return {lerpf(a.u, b.u, t),
            lerpAngle(a.shoulderX, b.shoulderX, t),
            lerpAngle(a.shoulderY, b.shoulderY, t),
            lerpAngle(a.shoulderZ, b.shoulderZ, t),
            lerpAngle(a.elbowX, b.elbowX, t),
            lerpAngle(a.wristX, b.wristX, t),
            lerpf(a.chestY, b.chestY, t)};
}

ArmKey sampleKeys(const ActionDef& d, float u) {
    for (int i = 0; i + 1 < d.keyCount; ++i) {
        const ArmKey& a = d.keys[i];
        const ArmKey& b = d.keys[i + 1];
        if (u <= b.u) return lerpKey(a, b, smoothstep01((u - a.u) / (b.u - a.u)));
    }
    return d.keys[d.keyCount - 1];
}

// Mezcla la pose de la acción sobre la de locomoción con peso `w`. Con `left`, en espejo
// sobre el brazo izquierdo (se invierten los giros en Y y Z).
void applyArm(Vec3* r, const ArmKey& k, float w, bool left = false) {
    Joint shoulder = left ? ShoulderL : ShoulderR, elbow = left ? ElbowL : ElbowR, wrist = left ? WristL : WristR;
    float m = left ? -1.0f : 1.0f;
    r[shoulder].x = lerpAngle(r[shoulder].x, k.shoulderX, w);
    r[shoulder].y = lerpAngle(r[shoulder].y, m * k.shoulderY, w);
    r[shoulder].z = lerpAngle(r[shoulder].z, m * k.shoulderZ, w);
    r[elbow].x = lerpAngle(r[elbow].x, k.elbowX, w);
    r[wrist].x = lerpAngle(r[wrist].x, k.wristX, w);
    r[Chest].y += m * k.chestY * w;
}

}  // namespace

void Character::reset() { *this = Character{}; }

void Character::setAim(AimMode mode, Vec3 target) {
    aimHeld_ = mode == AimMode::Gun;
    throwAim_ = mode == AimMode::Throw;
    if (mode != AimMode::None) aimTarget_ = target;
}

Mat4 Character::modelMatrix() const {
    Mat4 m = translate(body.pos) * rotateY(yaw);
    if (downTimer >= 0) return m * rotateX(-smoothstep01(downTimer / 0.6f) * kPi * 0.5f);
    if (rolling()) {
        // Voltereta: el cuerpo encogido gira una vuelta completa sobre el eje lateral,
        // alrededor de un punto a la altura de la cadera agachada.
        float u = clampf(rollTime_ / kRollDuration, 0, 1);
        float angle = smoothstep01(u) * 2.0f * kPi;
        const Vec3 pivot{0, 0.5f, 0};
        m = m * translate(pivot) * rotateX(angle) * translate(-pivot);
    }
    return m;
}

float Character::aimWeight() const {
    if (aimHeld_ || isGun(action_)) return 1.0f;
    return clampf(aimTimer_ / 0.15f, 0, 1);
}

bool Character::startAction(Action a, float faceYaw, float startU) {
    if (action_ != Action::None || a == Action::None || rolling()) return false;
    action_ = a;
    actionTime_ = startU * kActions[int(a)].duration;
    impactDone_ = false;
    yaw = wrapAngle(faceYaw);  // se gira hacia donde apunta la cámara
    if (isGun(a)) aimTimer_ = 0.7f;
    return true;
}

bool Character::attack(Item weapon, float faceYaw) {
    if (rolling()) return false;
    if (isMelee(action_)) {  // durante un golpe: se encadena el siguiente al terminar
        comboQueued_ = true;
        queuedYaw_ = faceYaw;
        return true;
    }
    if (action_ != Action::None) return false;
    // Fuera de la ventana de combo (o si el último golpe era de otra arma, que comboNext
    // no reconoce) se empieza por el primero.
    return startAction(comboNext(weapon, comboTimer_ < kComboWindow ? lastAttack_ : Action::None), faceYaw);
}

Action Character::takeImpact() {
    Action a = pendingImpact_;
    pendingImpact_ = Action::None;
    return a;
}

void Character::update(float dt, Vec3 moveDir, bool run, bool jump, float faceYaw, const World& world) {
    time_ += dt;

    float input = std::min(length(moveDir), 1.0f);
    if (input > 0.01f) moveDir_ = normalize(moveDir);
    // Componentes del movimiento respecto a la cámara: adelante y a la derecha.
    Vec3 camFwd{std::sin(faceYaw), 0, std::cos(faceYaw)}, camRight{-std::cos(faceYaw), 0, std::sin(faceYaw)};
    float along = dot(moveDir_, camFwd), side = dot(moveDir_, camRight);

    // Espacio mientras se va de lado (en el suelo y sin otra acción): voltereta.
    if (jump && body.grounded && !rolling() && action_ == Action::None && input > 0.3f &&
        std::fabs(side) > 0.6f) {
        rollTime_ = 0;
        rollDir_ = moveDir_;
        jump = false;
    }

    float maxSpeed = aimHeld_ || throwAim_ ? kAimSpeed : (run && along > 0.5f ? kRunSpeed : kWalkSpeed);
    if (along < -0.3f) maxSpeed *= kBackSpeedFactor;
    float targetSpeed = input > 0.01f ? maxSpeed * input : 0.0f;
    speed = moveTowards(speed, targetSpeed, kAcceleration * dt);

    // El cuerpo mira hacia donde mira la cámara (o hacia el objetivo al apuntar). Durante
    // la voltereta, hacia donde rueda.
    float targetYaw = faceYaw, turnRate = kTurnRate;
    if (aimHeld_ || throwAim_) {
        Vec3 toTarget = aimTarget_ - body.pos;
        targetYaw = std::atan2(toTarget.x, toTarget.z);
        turnRate = kAimTurnRate;
    }
    if (rolling()) {
        targetYaw = std::atan2(rollDir_.x, rollDir_.z);
        turnRate = kAimTurnRate;
        moveDir_ = rollDir_;
        speed = kRollSpeed * (1.0f - 0.4f * rollTime_ / kRollDuration);
        rollTime_ += dt;
        if (rollTime_ >= kRollDuration) rollTime_ = -1;
    }
    // Mientras golpea o lanza, el cuerpo mantiene la orientación de la acción.
    bool facingLocked = action_ != Action::None && !isGun(action_) && !rolling();
    if (!facingLocked) {
        float diff = wrapAngle(targetYaw - yaw);
        yaw = wrapAngle(yaw + clampf(diff, -turnRate * dt, turnRate * dt));
    }
    if (blocking()) speed = std::min(speed, kAimSpeed);
    // Empujones recibidos: se suman al movimiento y se frenan por rozamiento.
    knock_ = knock_ * std::exp(-8.0f * dt);
    body.vel.x = moveDir_.x * speed + knock_.x;
    body.vel.z = moveDir_.z * speed + knock_.z;
    if (knock_.y > 0) {
        body.vel.y += knock_.y;
        knock_.y = 0;
    }
    if (jump && body.grounded && !rolling()) body.vel.y = kJumpSpeed;

    moveBody(body, world, dt, kStepHeight);

    // La fase avanza con la distancia recorrida (solo pisando suelo), no con el tiempo:
    // así los pies no "patinan" sea cual sea la velocidad.
    if (body.grounded) {
        float runBlend = clampf((speed - kWalkSpeed) / (kRunSpeed - kWalkSpeed), 0, 1);
        // Longitud de un ciclo (dos pasos): trote ~2 m, sprint ~3,6 m.
        float cycleLength = lerpf(2.0f, 3.6f, runBlend);
        phase_ = std::fmod(phase_ + speed * dt * 2.0f * kPi / cycleLength, 2.0f * kPi);
    }
    airBlend_ = moveTowards(airBlend_, body.grounded ? 0.0f : 1.0f, 6.0f * dt);

    if (action_ != Action::None) {
        const ActionDef& d = kActions[int(action_)];
        actionTime_ += dt;
        float u = actionTime_ / d.duration;
        if (!impactDone_ && u >= d.impactU) {
            impactDone_ = true;
            pendingImpact_ = action_;
        }
        // Un golpe del combo en espera corta la recuperación del actual.
        bool chain = comboQueued_ && isMelee(action_) && impactDone_ && u >= 0.8f;
        if (u >= 1.0f || chain) {
            if (isMelee(action_)) {
                lastAttack_ = action_;
                comboTimer_ = 0;
            }
            Item weapon = meleeWeapon(action_);
            action_ = Action::None;
            if (comboQueued_) {
                comboQueued_ = false;
                if (weapon == item) startAction(comboNext(weapon, lastAttack_), queuedYaw_);
            }
        }
    } else {
        comboTimer_ += dt;
    }
    aimTimer_ = std::max(0.0f, aimTimer_ - dt);
    throwAimBlend_ = moveTowards(throwAimBlend_, throwAim_ && action_ == Action::None ? 1.0f : 0.0f, 8.0f * dt);
    blockBlend_ = moveTowards(blockBlend_, blocking() ? 1.0f : 0.0f, 12.0f * dt);

    animate();
    computeWorldMatrices(pose, modelMatrix(), joints);
    if (!aimHeld_) aimTarget_ = translationOf(joints[ShoulderR]) + forward() * 30.0f;
    aimArm();
}

// Cinemática inversa del brazo derecho: calcula el giro del hombro que hace que el brazo
// (eje -Y local, y con él el cañón de la pistola) apunte exactamente a aimTarget_.
// La dirección se pasa al espacio del pecho (padre del hombro) y se despeja:
//   rotY(y)·rotX(x)·(0,-1,0) = (-sin x·sin y, -cos x, -sin x·cos y) = d
void Character::aimArm() {
    bool armFree = action_ == Action::None || isGun(action_);
    float w = aimWeight();
    if (!armFree || w <= 0) return;

    Vec3 d = normalize(aimTarget_ - translationOf(joints[ShoulderR]));
    const float* c = joints[Chest].m;  // rotación inversa = transpuesta (columnas = ejes)
    Vec3 l{c[0] * d.x + c[1] * d.y + c[2] * d.z, c[4] * d.x + c[5] * d.y + c[6] * d.z,
           c[8] * d.x + c[9] * d.y + c[10] * d.z};
    float horizontal = std::sqrt(l.x * l.x + l.z * l.z);
    float shoulderY = std::atan2(l.x, l.z);
    float shoulderX = std::atan2(-horizontal, -l.y);

    // Retroceso del disparo: lo que la animación se aparta de la pose de apuntar.
    float recoilShoulder = 0, recoilElbow = 0;
    if (isGun(action_)) {
        const ActionDef& def = kActions[int(action_)];
        ArmKey k = sampleKeys(def, clampf(actionTime_ / def.duration, 0, 1));
        recoilShoulder = k.shoulderX - def.keys[0].shoulderX;
        recoilElbow = k.elbowX - def.keys[0].elbowX;
    }

    Vec3* r = pose.rotation;
    r[ShoulderR].x = lerpAngle(r[ShoulderR].x, shoulderX + recoilShoulder, w);
    r[ShoulderR].y = lerpAngle(r[ShoulderR].y, shoulderY, w);
    r[ShoulderR].z = lerpAngle(r[ShoulderR].z, 0, w);
    r[ElbowR].x = lerpAngle(r[ElbowR].x, recoilElbow, w);
    r[WristR].x = lerpAngle(r[WristR].x, 0, w);
    computeWorldMatrices(pose, modelMatrix(), joints);
}

// Recordatorio de signos (ver vmath.h): rotar +X lleva un miembro que cuelga hacia
// atrás (-Z); rotar +Z lo lleva hacia +X (la izquierda del personaje).
void Character::animate() {
    pose = Pose{};
    Vec3* r = pose.rotation;

    float walk = clampf(speed / kWalkSpeed, 0, 1) * (1.0f - airBlend_);  // 0 = parado
    float run = clampf((speed - kWalkSpeed) / (kRunSpeed - kWalkSpeed), 0, 1);
    float s = std::sin(phase_), c = std::cos(phase_);

    float legSwing = lerpf(0.55f, 0.85f, run) * walk;
    float kneeBend = lerpf(0.9f, 1.6f, run) * walk;
    float armSwing = lerpf(0.5f, 1.0f, run) * walk;

    // Dirección del paso respecto al cuerpo: adelante/atrás (`along`) y de lado (`side`,
    // positivo hacia la izquierda del personaje, +X local).
    Vec3 fwd = forward(), left{std::cos(yaw), 0, -std::sin(yaw)};
    float along = dot(moveDir_, fwd), side = dot(moveDir_, left);

    // Piernas en oposición de fase. La rodilla se flexiona cuando la pierna avanza en el
    // sentido de la marcha (hacia atrás se invierte el ciclo).
    float dirSign = along < 0 ? -1.0f : 1.0f;
    float fa = std::fabs(along), sa = std::fabs(side);
    r[HipL].x = -legSwing * s * along;
    r[HipR].x = legSwing * s * along;
    r[KneeL].x = kneeBend * (fa * std::max(0.0f, c * dirSign) + sa * 0.6f * std::max(0.0f, s)) + 0.05f;
    r[KneeR].x = kneeBend * (fa * std::max(0.0f, -c * dirSign) + sa * 0.6f * std::max(0.0f, -s)) + 0.05f;
    r[AnkleL].x = -0.6f * (r[HipL].x + r[KneeL].x) * walk;
    r[AnkleR].x = -0.6f * (r[HipR].x + r[KneeR].x) * walk;
    // Paso lateral: las piernas se abren y cierran alternándose hacia el lado de la marcha.
    float sideSwing = 0.45f * walk * side;
    r[HipL].z = 0.03f + sideSwing * std::max(0.0f, s);
    r[HipR].z = -0.03f + sideSwing * std::max(0.0f, -s);

    // Brazos al contrario que las piernas del mismo lado.
    r[ShoulderL].x = armSwing * s * along;
    r[ShoulderR].x = -armSwing * s * along;
    r[ShoulderL].z = 0.08f;
    r[ShoulderR].z = -0.08f;
    float elbow = 0.1f + lerpf(0.6f, 1.4f, run) * walk;
    r[ElbowL].x = -elbow;
    r[ElbowR].x = -elbow;

    // Torso: inclinación hacia delante al correr y torsión pelvis/pecho contrapuesta.
    r[Spine].x = (0.08f + 0.2f * run) * walk;
    r[Pelvis].y = 0.12f * s * walk;
    r[Chest].y = -0.22f * s * walk;
    // La cabeza compensa para mirar al frente.
    r[Neck].x = -0.6f * r[Spine].x;
    r[Head].y = -(r[Pelvis].y + r[Chest].y);

    // Balanceo vertical: el cuerpo baja cuando las piernas están más abiertas.
    pose.rootOffset.y = -lerpf(0.03f, 0.06f, run) * walk * std::fabs(s);

    // Respiración en reposo.
    float idle = (1.0f - walk) * (1.0f - airBlend_);
    float breath = std::sin(time_ * 2.0f);
    r[Chest].x += 0.025f * breath * idle;
    r[ShoulderL].z += 0.02f * breath * idle;
    r[ShoulderR].z -= 0.02f * breath * idle;

    // En el aire: piernas recogidas (una más que otra) y brazos abiertos para equilibrar.
    float a = airBlend_;
    r[HipL].x = lerpf(r[HipL].x, -0.8f, a);
    r[HipR].x = lerpf(r[HipR].x, -0.25f, a);
    r[KneeL].x = lerpf(r[KneeL].x, 1.3f, a);
    r[KneeR].x = lerpf(r[KneeR].x, 0.7f, a);
    r[ShoulderL].z += 0.6f * a;
    r[ShoulderR].z -= 0.6f * a;
    r[ElbowL].x -= 0.4f * a;
    r[ElbowR].x -= 0.4f * a;

    // Voltereta: cuerpo hecho una bola (el giro lo da modelMatrix()).
    if (rolling()) {
        float u = clampf(rollTime_ / kRollDuration, 0, 1);
        float w = clampf(std::min(u / 0.15f, (1.0f - u) / 0.2f), 0, 1);
        r[Spine].x = lerpf(r[Spine].x, 0.7f, w);
        r[Chest].x = lerpf(r[Chest].x, 0.4f, w);
        r[Neck].x = lerpf(r[Neck].x, 0.5f, w);
        for (Joint j : {HipL, HipR}) r[j].x = lerpf(r[j].x, -2.0f, w);
        for (Joint j : {KneeL, KneeR}) r[j].x = lerpf(r[j].x, 2.3f, w);
        for (Joint j : {ShoulderL, ShoulderR}) r[j].x = lerpf(r[j].x, -1.2f, w);
        for (Joint j : {ElbowL, ElbowR}) r[j].x = lerpf(r[j].x, -1.6f, w);
        pose.rootOffset.y -= 0.35f * w;
    }

    // Postura según lo que lleva en la mano.
    if (item == Item::Sword || item == Item::Hammer) {  // espada/martillo en guardia, codo doblado
        r[ShoulderR].x = r[ShoulderR].x * 0.4f - 0.3f;
        r[ShoulderR].z = -0.1f;
        r[ElbowR].x = -1.3f;
    }

    // Acción en curso. Disparar/apuntar no usa keyframes para el brazo: lo resuelve
    // aimArm() con cinemática inversa; aquí solo se adelanta el hombro girando el pecho.
    if (isGun(action_) || (action_ == Action::None && aiming())) {
        r[Chest].y += kShootKeys[0].chestY * aimWeight();
    } else if (action_ == Action::None && throwAimBlend_ > 0) {
        // Apuntando un lanzamiento: brazo atrás preparado (la pose de carga del lanzamiento)
        // y el izquierdo adelantado señalando el objetivo.
        float w = throwAimBlend_;
        applyArm(r, kThrowKeys[1], w);
        r[ShoulderL].x = lerpAngle(r[ShoulderL].x, -1.3f, w);
        r[ShoulderL].z = lerpAngle(r[ShoulderL].z, 0.2f, w);
        r[ElbowL].x = lerpAngle(r[ElbowL].x, -0.3f, w);
    } else if (action_ != Action::None) {
        const ActionDef& d = kActions[int(action_)];
        float u = clampf(actionTime_ / d.duration, 0, 1);
        float wIn = d.blendIn > 0 ? u / d.blendIn : 1.0f;
        float wOut = d.blendOut > 0 ? (1.0f - u) / d.blendOut : 1.0f;
        float w = clampf(std::min(wIn, wOut), 0, 1);
        applyArm(r, sampleKeys(d, u), w, d.leftArm);
        if (d.guardOther) {
            Joint guardShoulder = d.leftArm ? ShoulderR : ShoulderL, guardElbow = d.leftArm ? ElbowR : ElbowL;
            r[guardShoulder].x = lerpAngle(r[guardShoulder].x, -0.6f, w);
            r[guardElbow].x = lerpAngle(r[guardElbow].x, -1.9f, w);
        }
    }
    // Defensa con la espada: hoja vertical delante del pecho, la otra mano en el pomo.
    if (blockBlend_ > 0) {
        float w = blockBlend_;
        applyArm(r, {0, -1.0f, 0.45f, 0.0f, -0.6f, 0.0f, -0.25f}, w);
        r[ShoulderL].x = lerpAngle(r[ShoulderL].x, -0.9f, w);
        r[ShoulderL].y = lerpAngle(r[ShoulderL].y, -0.6f, w);
        r[ElbowL].x = lerpAngle(r[ElbowL].x, -0.9f, w);
        r[Spine].x += 0.1f * w;
        pose.rootOffset.y -= 0.06f * w;  // algo agachado
    }
}
