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
    bool guardLeft;   // levanta el brazo izquierdo en guardia
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
const ArmKey kSlashKeys[] = {
    {0.00f,   -0.30f,  0.0f,  -0.10f, -1.30f, 0.0f,  0.00f},  // guardia, espada vertical
    {0.35f,   -2.90f,  0.3f,  -0.30f, -0.40f, 0.0f, -0.30f},  // espada levantada atrás
    {0.60f,   -0.40f, -0.4f,  -0.10f, -0.10f, 1.0f,  0.50f},  // tajo hacia abajo
    {1.00f,   -0.30f,  0.0f,  -0.10f, -1.30f, 0.0f,  0.00f},
};
const ArmKey kShootKeys[] = {
    {0.00f,   -1.55f,  0.15f,  0.00f,  0.00f, 0.0f,  0.25f},  // apuntando
    {0.15f,   -1.80f,  0.15f,  0.00f, -0.20f, 0.0f,  0.25f},  // retroceso
    {1.00f,   -1.55f,  0.15f,  0.00f,  0.00f, 0.0f,  0.25f},
};
const ArmKey kThrowKeys[] = {
    {0.00f,   -0.20f,  0.0f,  -0.10f, -0.50f, 0.0f,  0.00f},
    {0.40f,    2.50f,  0.0f,  -0.20f, -1.30f, 0.0f, -0.40f},  // brazo arriba y atrás
    {0.55f,    4.30f,  0.0f,  -0.10f, -0.10f, 0.0f,  0.40f},  // suelta la piedra
    {1.00f,    6.08f,  0.0f,  -0.10f, -0.30f, 0.0f,  0.00f},  // acompaña hacia abajo
};

const ActionDef kActions[] = {
    // duración impacto entrada salida  keys
    {0.00f,    0.00f,  0.00f,  0.00f,  nullptr,     0, false},  // None
    {0.35f,    0.40f,  0.10f,  0.25f,  kPunchKeys,  3, true},   // Punch
    {0.50f,    0.50f,  0.05f,  0.20f,  kSlashKeys,  4, false},  // Slash
    {0.18f,    0.00f,  0.00f,  0.00f,  kShootKeys,  3, false},  // Shoot
    {0.55f,    0.55f,  0.10f,  0.20f,  kThrowKeys,  4, false},  // Throw
};
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

// Mezcla la pose de la acción sobre la de locomoción con peso `w`.
void applyArm(Vec3* r, const ArmKey& k, float w) {
    r[ShoulderR].x = lerpAngle(r[ShoulderR].x, k.shoulderX, w);
    r[ShoulderR].y = lerpAngle(r[ShoulderR].y, k.shoulderY, w);
    r[ShoulderR].z = lerpAngle(r[ShoulderR].z, k.shoulderZ, w);
    r[ElbowR].x = lerpAngle(r[ElbowR].x, k.elbowX, w);
    r[WristR].x = lerpAngle(r[WristR].x, k.wristX, w);
    r[Chest].y += k.chestY * w;
}

}  // namespace

void Character::reset() { *this = Character{}; }

void Character::setAim(bool held, Vec3 target) {
    aimHeld_ = held;
    if (held) aimTarget_ = target;
}

float Character::aimWeight() const {
    if (aimHeld_ || action_ == Action::Shoot) return 1.0f;
    return clampf(aimTimer_ / 0.15f, 0, 1);
}

bool Character::startAction(Action a, float faceYaw) {
    if (action_ != Action::None || a == Action::None) return false;
    action_ = a;
    actionTime_ = 0;
    impactDone_ = false;
    yaw = wrapAngle(faceYaw);  // se gira hacia donde apunta la cámara
    if (a == Action::Shoot) aimTimer_ = 0.7f;
    return true;
}

Action Character::takeImpact() {
    Action a = pendingImpact_;
    pendingImpact_ = Action::None;
    return a;
}

void Character::update(float dt, Vec3 moveDir, bool run, bool jump, const World& world) {
    time_ += dt;

    float input = std::min(length(moveDir), 1.0f);
    float maxSpeed = aimHeld_ ? kAimSpeed : (run ? kRunSpeed : kWalkSpeed);
    float targetSpeed = input > 0.01f ? maxSpeed * input : 0.0f;
    speed = moveTowards(speed, targetSpeed, kAcceleration * dt);

    // Mientras golpea, lanza o apunta, el cuerpo mantiene la orientación de la acción
    // y se desplaza "de lado" si hace falta.
    bool facingLocked = (action_ != Action::None && action_ != Action::Shoot) || aiming();
    if (aimHeld_) {
        Vec3 toTarget = aimTarget_ - body.pos;
        float diff = wrapAngle(std::atan2(toTarget.x, toTarget.z) - yaw);
        yaw = wrapAngle(yaw + clampf(diff, -kAimTurnRate * dt, kAimTurnRate * dt));
    }
    if (input > 0.01f) {
        moveDir_ = normalize(moveDir);
        if (!facingLocked) {
            float targetYaw = std::atan2(moveDir_.x, moveDir_.z);
            float diff = wrapAngle(targetYaw - yaw);
            yaw = wrapAngle(yaw + clampf(diff, -kTurnRate * dt, kTurnRate * dt));
        }
    }
    body.vel.x = moveDir_.x * speed;
    body.vel.z = moveDir_.z * speed;
    if (jump && body.grounded) body.vel.y = kJumpSpeed;

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
        if (u >= 1.0f) action_ = Action::None;
    }
    aimTimer_ = std::max(0.0f, aimTimer_ - dt);

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
    bool armFree = action_ == Action::None || action_ == Action::Shoot;
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
    if (action_ == Action::Shoot) {
        const ActionDef& def = kActions[int(Action::Shoot)];
        ArmKey k = sampleKeys(def, clampf(actionTime_ / def.duration, 0, 1));
        recoilShoulder = k.shoulderX - kShootKeys[0].shoulderX;
        recoilElbow = k.elbowX - kShootKeys[0].elbowX;
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

    // Piernas en oposición de fase. La rodilla se flexiona cuando la pierna avanza.
    r[HipL].x = -legSwing * s;
    r[HipR].x = legSwing * s;
    r[KneeL].x = kneeBend * std::max(0.0f, c) + 0.05f;
    r[KneeR].x = kneeBend * std::max(0.0f, -c) + 0.05f;
    r[AnkleL].x = -0.6f * (r[HipL].x + r[KneeL].x) * walk;
    r[AnkleR].x = -0.6f * (r[HipR].x + r[KneeR].x) * walk;
    r[HipL].z = 0.03f;
    r[HipR].z = -0.03f;

    // Brazos al contrario que las piernas del mismo lado.
    r[ShoulderL].x = armSwing * s;
    r[ShoulderR].x = -armSwing * s;
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

    // Postura según lo que lleva en la mano.
    if (item == Item::Sword) {  // espada en guardia, codo doblado
        r[ShoulderR].x = r[ShoulderR].x * 0.4f - 0.3f;
        r[ShoulderR].z = -0.1f;
        r[ElbowR].x = -1.3f;
    }

    // Acción en curso. Disparar/apuntar no usa keyframes para el brazo: lo resuelve
    // aimArm() con cinemática inversa; aquí solo se adelanta el hombro girando el pecho.
    if (action_ == Action::Shoot || (action_ == Action::None && aiming())) {
        r[Chest].y += kShootKeys[0].chestY * aimWeight();
    } else if (action_ != Action::None) {
        const ActionDef& d = kActions[int(action_)];
        float u = clampf(actionTime_ / d.duration, 0, 1);
        float wIn = d.blendIn > 0 ? u / d.blendIn : 1.0f;
        float wOut = d.blendOut > 0 ? (1.0f - u) / d.blendOut : 1.0f;
        float w = clampf(std::min(wIn, wOut), 0, 1);
        applyArm(r, sampleKeys(d, u), w);
        if (d.guardLeft) {
            r[ShoulderL].x = lerpAngle(r[ShoulderL].x, -0.6f, w);
            r[ElbowL].x = lerpAngle(r[ElbowL].x, -1.9f, w);
        }
    }
}
