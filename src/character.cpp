#include "character.h"

#include <algorithm>

namespace {

constexpr float kAcceleration = 60.0f;  // m/s²: respuesta casi instantánea
constexpr float kTurnRate = 18.0f;      // rad/s
constexpr float kDashTime = 0.14f;      // el acercamiento al objetivo dura esto
constexpr float kAirHover = 0.85f;      // fracción de la gravedad que se anula en un golpe aéreo

// Pose del brazo del arma (derecho) y giro del pecho en un instante `u` ∈ [0,1].
struct ArmKey {
    float u;
    float shoulderX, shoulderY, shoulderZ, elbowX, wristX, chestY;
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
};

// Recordatorio de signos: rotar +X lleva hacia atrás un brazo que cuelga; -X hacia
// delante (-1,57 = horizontal al frente). -Z lo abre hacia la derecha del personaje.
// La hoja sale de la mano perpendicular al antebrazo (+Z local de la muñeca); con la
// muñeca a +1,3 queda en la prolongación del brazo.
// clang-format off
//                                u     hombroX hombroY hombroZ codoX  muñecaX pechoY
const ArmKey kSlash1Keys[] = {  // barrido de derecha a izquierda
    {0.00f, -0.30f,  0.00f, -0.10f, -1.30f, 0.0f,  0.00f},
    {0.25f, -1.30f, -1.40f,  0.00f, -0.30f, 1.3f, -0.60f},
    {0.50f, -1.40f,  1.00f,  0.00f, -0.20f, 1.3f,  0.70f},
    {1.00f, -0.30f,  0.00f, -0.10f, -1.30f, 0.0f,  0.00f},
};
const ArmKey kSlash2Keys[] = {  // revés de izquierda a derecha
    {0.00f, -1.30f,  0.90f,  0.00f, -0.30f, 1.3f,  0.60f},
    {0.20f, -1.50f,  1.10f,  0.00f, -0.40f, 1.3f,  0.75f},
    {0.50f, -1.30f, -1.40f,  0.00f, -0.20f, 1.3f, -0.70f},
    {1.00f, -0.30f,  0.00f, -0.10f, -1.30f, 0.0f,  0.00f},
};
const ArmKey kSpinKeys[] = {    // brazo abierto: el cuerpo gira y la hoja barre un círculo
    {0.00f, -0.30f,  0.00f, -0.10f, -1.30f, 0.0f,  0.00f},
    {0.15f, -0.40f,  0.00f, -1.45f, -0.10f, 1.3f, -0.30f},
    {0.80f, -0.40f,  0.00f, -1.45f, -0.10f, 1.3f, -0.30f},
    {1.00f, -0.30f,  0.00f, -0.10f, -1.30f, 0.0f,  0.00f},
};
const ArmKey kOverheadKeys[] = {  // arma por encima de la cabeza y golpe hacia abajo
    {0.00f, -0.30f,  0.00f, -0.10f, -1.30f, 0.0f,  0.00f},
    {0.35f, -3.00f,  0.10f, -0.20f, -0.60f, 0.3f, -0.20f},
    {0.62f, -3.00f,  0.10f, -0.20f, -0.60f, 0.3f, -0.20f},
    {0.75f, -0.60f,  0.00f, -0.10f,  0.00f, 1.2f,  0.30f},
    {1.00f, -0.30f,  0.00f, -0.10f, -1.30f, 0.0f,  0.00f},
};
const ArmKey kChopKeys[] = {    // tajo vertical (aire)
    {0.00f, -0.30f,  0.00f, -0.10f, -1.30f, 0.0f,  0.00f},
    {0.30f, -2.90f,  0.30f, -0.30f, -0.40f, 0.0f, -0.30f},
    {0.55f, -0.40f, -0.40f, -0.10f, -0.10f, 1.0f,  0.50f},
    {1.00f, -0.30f,  0.00f, -0.10f, -1.30f, 0.0f,  0.00f},
};
const ArmKey kCastKeys[] = {    // brazo al frente señalando con el arma
    {0.00f, -0.30f,  0.00f, -0.10f, -1.30f, 0.0f,  0.00f},
    {0.30f, -1.65f,  0.15f,  0.00f,  0.00f, 1.3f,  0.35f},
    {0.70f, -1.65f,  0.15f,  0.00f,  0.00f, 1.3f,  0.35f},
    {1.00f, -0.30f,  0.00f, -0.10f, -1.30f, 0.0f,  0.00f},
};

const MoveDef kMoves[int(Move::Count)] = {
//   dur    impacto entrada salida keys              n  giro       volt.     tramo        salto lean
    {0.00f, 0.00f, 0.00f, 0.00f, nullptr,        0, 0,          0,        0.00f, 1.00f, 0.0f, 0.00f},  // None
    {0.36f, 0.42f, 0.08f, 0.25f, kSlash1Keys,    4, 0,          0,        0.00f, 1.00f, 0.0f, 0.15f},  // Slash1
    {0.36f, 0.42f, 0.05f, 0.25f, kSlash2Keys,    4, 0,          0,        0.00f, 1.00f, 0.0f, 0.15f},  // Slash2
    {0.50f, 0.45f, 0.05f, 0.20f, kSpinKeys,      4, 2 * kPi,    0,        0.12f, 0.75f, 0.0f, 0.10f},  // Slash3
    {0.80f, 0.70f, 0.05f, 0.20f, kOverheadKeys,  5, 0,          2 * kPi,  0.05f, 0.55f, 5.5f, 0.25f},  // Finisher
    {0.34f, 0.45f, 0.08f, 0.25f, kChopKeys,      4, 0,          0,        0.00f, 1.00f, 0.0f, 0.20f},  // Air1
    {0.34f, 0.42f, 0.05f, 0.25f, kSlash1Keys,    4, 0,          0,        0.00f, 1.00f, 0.0f, 0.10f},  // Air2
    {0.60f, 0.50f, 0.05f, 0.20f, kSpinKeys,      4, 4 * kPi,    0,        0.10f, 0.80f, 0.0f, 0.10f},  // AirFinisher
    {0.50f, 0.35f, 0.10f, 0.25f, kCastKeys,      4, 0,          0,        0.00f, 1.00f, 0.0f, 0.00f},  // Cast
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

ArmKey sampleKeys(const MoveDef& d, float u) {
    for (int i = 0; i + 1 < d.keyCount; ++i) {
        const ArmKey& a = d.keys[i];
        const ArmKey& b = d.keys[i + 1];
        if (u <= b.u) return lerpKey(a, b, smoothstep01((u - a.u) / (b.u - a.u)));
    }
    return d.keys[d.keyCount - 1];
}

// Mezcla la pose del brazo derecho sobre la de locomoción con peso `w`.
void applyArm(Vec3* r, const ArmKey& k, float w) {
    r[ShoulderR].x = lerpAngle(r[ShoulderR].x, k.shoulderX, w);
    r[ShoulderR].y = lerpAngle(r[ShoulderR].y, k.shoulderY, w);
    r[ShoulderR].z = lerpAngle(r[ShoulderR].z, k.shoulderZ, w);
    r[ElbowR].x = lerpAngle(r[ElbowR].x, k.elbowX, w);
    r[WristR].x = lerpAngle(r[WristR].x, k.wristX, w);
    r[Chest].y += k.chestY * w;
}

float moveDuration(Move m, float attackSpeed) {
    float d = kMoves[int(m)].duration;
    return isAttack(m) ? d / attackSpeed : d;
}

}  // namespace

void Character::reset() { *this = Character{}; }

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
    if (move_ != Move::None) {
        const MoveDef& d = kMoves[int(move_)];
        float t = smoothstep01((moveU() - d.turnFrom) / (d.turnTo - d.turnFrom));
        if (d.spin != 0) m = m * rotateY(d.spin * t);
        if (d.flip != 0) {  // voltereta alrededor del centro del cuerpo
            const Vec3 pivot{0, 0.9f, 0};
            m = m * translate(pivot) * rotateX(d.flip * t) * translate(-pivot);
        }
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
    if (comboStep_ >= comboHits) return Move::Finisher;
    const Move seq[3] = {Move::Slash1, Move::Slash2, Move::Slash3};
    return seq[comboStep_ % 3];
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
    } else if (isAttack(m)) {
        comboStep_ = m == Move::Finisher ? 0 : comboStep_ + 1;
    }
    if (d.hop > 0 && body.grounded) body.vel.y = d.hop;
    if (dash > 0.05f) {
        dashTime_ = kDashTime;
        dashVel_ = forward() * (dash / kDashTime);
    } else {
        dashTime_ = 0;
        dashVel_ = {};
    }
}

bool Character::attack(float faceYaw, float dash) {
    if (rolling() || hurt() || downTimer >= 0) return false;
    if (isAttack(move_)) {  // durante un golpe: se encadena el siguiente al terminar
        if (move_ == Move::AirFinisher) return false;
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

void Character::update(float dt, Vec3 moveDir, bool jump, const World& world) {
    time_ += dt;
    hurtTimer_ = std::max(0.0f, hurtTimer_ - dt);
    float input = downTimer >= 0 ? 0.0f : std::min(length(moveDir), 1.0f);
    if (input > 0.01f) moveDir_ = normalize(moveDir);

    // Correr hacia donde se pide; el cuerpo gira hacia la dirección de la marcha.
    bool canMove = !busy() && !guarding();
    float targetSpeed = canMove ? kRunSpeed * input : 0.0f;
    speed = moveTowards(speed, targetSpeed, kAcceleration * dt);
    if (canMove && input > 0.01f) {
        float targetYaw = std::atan2(moveDir_.x, moveDir_.z);
        yaw = wrapAngle(yaw + clampf(wrapAngle(targetYaw - yaw), -kTurnRate * dt, kTurnRate * dt));
    }
    Vec3 dir = moveDir_;
    if (rolling()) {
        float u = rollTime_ / kRollDuration;
        dir = rollDir_;
        speed = kRollSpeed * (1.0f - 0.5f * u);
        yaw = std::atan2(rollDir_.x, rollDir_.z);
        rollTime_ += dt;
        if (rollTime_ >= kRollDuration) {
            rollTime_ = -1;
            speed = 0;
        }
    }

    knock_ = knock_ * std::exp(-8.0f * dt);
    if (dashTime_ > 0) {
        dashTime_ -= dt;
        if (dashTime_ <= 0) dashVel_ = {};
    }
    body.vel.x = dir.x * speed + knock_.x + dashVel_.x;
    body.vel.z = dir.z * speed + knock_.z + dashVel_.z;
    if (knock_.y > 0) {
        body.vel.y += knock_.y;
        knock_.y = 0;
    }
    if (jump && body.grounded && canMove) body.vel.y = kJumpSpeed;
    // Golpe aéreo: casi sin gravedad mientras dura (se queda flotando, como en el género).
    if (isAirAttack(move_)) {
        body.vel.y += kGravity * kAirHover * dt;
        body.vel.y = std::max(body.vel.y, -1.5f);
    }

    moveBody(body, world, dt, kStepHeight);
    if (body.grounded && !isAirAttack(move_)) {
        airStep_ = 0;
        airComboSpent_ = false;
    }

    // La fase avanza con la distancia recorrida (solo pisando suelo): los pies no patinan.
    if (body.grounded) phase_ = std::fmod(phase_ + speed * dt * 2.0f * kPi / 2.6f, 2.0f * kPi);
    airBlend_ = moveTowards(airBlend_, body.grounded ? 0.0f : 1.0f, 6.0f * dt);

    if (move_ != Move::None) {
        const MoveDef& d = kMoves[int(move_)];
        moveTime_ += dt;
        float u = moveTime_ / moveDuration(move_, attackSpeed);
        if (!impactDone_ && u >= d.impactU) {
            impactDone_ = true;
            pendingImpact_ = move_;
        }
        // Un golpe aéreo termina al tocar el suelo.
        if (isAirAttack(move_) && body.grounded && impactDone_) u = 1;
        // Un golpe en cola corta la recuperación del actual.
        bool chain = queued_ && isAttack(move_) && impactDone_ && u >= 0.6f;
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
    guardBlend_ = moveTowards(guardBlend_, guarding() ? 1.0f : 0.0f, 14.0f * dt);

    animate();
    computeWorldMatrices(pose, modelMatrix(), joints);
}

// Recordatorio de signos (ver vmath.h): rotar +X lleva un miembro que cuelga hacia
// atrás (-Z); rotar +Z lo lleva hacia +X (la izquierda del personaje).
void Character::animate() {
    pose = Pose{};
    Vec3* r = pose.rotation;

    float run = clampf(speed / kRunSpeed, 0, 1) * (1.0f - airBlend_);
    float s = std::sin(phase_), c = std::cos(phase_);

    // Carrera: piernas en oposición de fase, rodilla flexionada al adelantar la pierna.
    float legSwing = 0.8f * run, kneeBend = 1.5f * run;
    r[HipL].x = -legSwing * s;
    r[HipR].x = legSwing * s;
    r[KneeL].x = kneeBend * std::max(0.0f, c) + 0.05f;
    r[KneeR].x = kneeBend * std::max(0.0f, -c) + 0.05f;
    r[AnkleL].x = -0.6f * (r[HipL].x + r[KneeL].x) * run;
    r[AnkleR].x = -0.6f * (r[HipR].x + r[KneeR].x) * run;
    r[HipL].z = 0.04f;
    r[HipR].z = -0.04f;

    // Brazo libre al contrario que su pierna; el del arma la lleva atrás al correr.
    r[ShoulderL].x = 0.9f * run * s;
    r[ShoulderL].z = 0.1f + 0.15f * run;
    r[ElbowL].x = -0.2f - 1.0f * run;
    r[ShoulderR].x = lerpf(-0.3f, 0.5f, run) - 0.3f * run * s;
    r[ShoulderR].z = -0.15f - 0.2f * run;
    r[ElbowR].x = lerpf(-1.3f, -0.6f, run);
    r[WristR].x = lerpf(0.75f, 0.9f, run);  // en reposo la hoja apunta al frente y abajo; corriendo, atrás

    // Torso inclinado hacia delante al correr; pelvis y pecho en torsión contrapuesta.
    r[Spine].x = 0.25f * run;
    r[Pelvis].y = 0.12f * s * run;
    r[Chest].y = -0.2f * s * run;
    r[Neck].x = -0.6f * r[Spine].x;
    r[Head].y = -(r[Pelvis].y + r[Chest].y);
    pose.rootOffset.y = -0.05f * run * std::fabs(s);

    // Reposo: postura de combate con las piernas abiertas y respiración.
    float idle = (1.0f - run) * (1.0f - airBlend_);
    float breath = std::sin(time_ * 2.2f);
    r[HipL].z += 0.12f * idle;
    r[HipR].z -= 0.12f * idle;
    r[KneeL].x += 0.15f * idle;
    r[KneeR].x += 0.15f * idle;
    r[AnkleL].z -= 0.1f * idle;
    r[AnkleR].z += 0.1f * idle;
    pose.rootOffset.y -= 0.03f * idle;
    r[Chest].x += 0.03f * breath * idle;
    r[Pelvis].y += 0.25f * idle;  // de medio lado, con el arma delante
    r[Chest].y -= 0.1f * idle;
    r[Head].y -= 0.15f * idle;
    r[ShoulderL].z += 0.04f * breath * idle;

    // En el aire: piernas recogidas y brazos abiertos.
    float a = airBlend_;
    r[HipL].x = lerpf(r[HipL].x, -0.9f, a);
    r[HipR].x = lerpf(r[HipR].x, -0.3f, a);
    r[KneeL].x = lerpf(r[KneeL].x, 1.4f, a);
    r[KneeR].x = lerpf(r[KneeR].x, 0.8f, a);
    r[ShoulderL].z += 0.7f * a;
    r[ShoulderR].z -= 0.4f * a;

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

    // Movimiento en curso: brazo del arma por keyframes, tronco inclinado y piernas en
    // posición de golpe (abiertas en el suelo, recogidas en el aire).
    if (move_ != Move::None) {
        const MoveDef& d = kMoves[int(move_)];
        float u = moveU();
        float wIn = d.blendIn > 0 ? u / d.blendIn : 1.0f;
        float wOut = d.blendOut > 0 ? (1.0f - u) / d.blendOut : 1.0f;
        float w = clampf(std::min(wIn, wOut), 0, 1);
        applyArm(r, sampleKeys(d, u), w);
        r[Spine].x += d.lean * w;
        r[ShoulderL].x = lerpAngle(r[ShoulderL].x, -0.5f, w);  // brazo libre equilibrando
        r[ShoulderL].z = lerpAngle(r[ShoulderL].z, 0.9f, w);
        r[ElbowL].x = lerpAngle(r[ElbowL].x, -0.5f, w);
        if (body.grounded) {
            r[HipL].x = lerpAngle(r[HipL].x, -0.5f, w);  // pierna izquierda adelantada
            r[HipR].x = lerpAngle(r[HipR].x, 0.35f, w);
            r[KneeL].x = lerpAngle(r[KneeL].x, 0.6f, w);
            r[KneeR].x = lerpAngle(r[KneeR].x, 0.3f, w);
            pose.rootOffset.y -= 0.08f * w;
        }
    }

    // Guardia: hoja horizontal delante del cuerpo, agachado.
    if (guardBlend_ > 0) {
        float w = guardBlend_;
        applyArm(r, {0, -1.2f, 0.7f, 0.0f, -0.9f, 1.3f, -0.3f}, w);
        r[ShoulderL].x = lerpAngle(r[ShoulderL].x, -1.0f, w);
        r[ShoulderL].y = lerpAngle(r[ShoulderL].y, -0.5f, w);
        r[ElbowL].x = lerpAngle(r[ElbowL].x, -1.0f, w);
        r[Spine].x += 0.15f * w;
        r[KneeL].x += 0.35f * w;
        r[KneeR].x += 0.35f * w;
        r[HipL].x -= 0.2f * w;
        r[HipR].x -= 0.2f * w;
        pose.rootOffset.y -= 0.1f * w;
    }

    // Golpe recibido: echa el tronco y la cabeza atrás.
    if (hurtTimer_ > 0) {
        float w = std::sin(clampf(hurtTimer_ / 0.35f, 0, 1) * kPi);
        r[Spine].x -= 0.35f * w;
        r[Neck].x -= 0.3f * w;
        r[ShoulderL].z += 0.5f * w;
        r[ShoulderR].z -= 0.5f * w;
    }
    // Derrotado: brazos y cabeza sueltos.
    if (downTimer >= 0) {
        float w = clampf(downTimer / 0.4f, 0, 1);
        r[ShoulderL].z = lerpf(r[ShoulderL].z, 1.2f, w);
        r[ShoulderR].z = lerpf(r[ShoulderR].z, -1.2f, w);
        r[Neck].x = lerpf(r[Neck].x, -0.4f, w);
    }
}
