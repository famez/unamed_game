// Trepar: escaleras de gato, pegarse a una pared y saltar desde ella, y encaramarse a un borde.
// Ver Character::Climb.

#include "actors/character.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace {

constexpr float kLadderGap = 0.08f;  // de la cara de la pared al cuerpo, en la escalera
constexpr float kRungStep = 0.6f;    // metros por ciclo de manos y pies (dos peldaños)

// La cara vertical de `b` que tiene delante quien está en `p` (hacia fuera) y lo lejos que
// está de ella; false si está dentro de la caja.
bool faceOf(const AABB& b, Vec3 p, Vec3& normal, float& gap) {
    float out[4] = {b.min.x - p.x, p.x - b.max.x, b.min.z - p.z, p.z - b.max.z};
    const Vec3 normals[4] = {{-1, 0, 0}, {1, 0, 0}, {0, 0, -1}, {0, 0, 1}};
    int face = 0;
    for (int k = 1; k < 4; ++k)
        if (out[k] > out[face]) face = k;
    if (out[face] < -0.02f) return false;
    // De frente a la cara, no por una esquina: dentro de su anchura.
    bool alongZ = face < 2;
    float lo = alongZ ? b.min.z : b.min.x, hi = alongZ ? b.max.z : b.max.x, at = alongZ ? p.z : p.x;
    if (at < lo - 0.05f || at > hi + 0.05f) return false;
    normal = normals[face];
    gap = out[face];
    return true;
}

// ¿Cabe un cuerpo de pie en `pos` sin tocar ninguna caja?
bool freeAt(const World& w, const Body& body, Vec3 pos) {
    AABB me{{pos.x - body.radius + 0.02f, pos.y + 0.02f, pos.z - body.radius + 0.02f},
            {pos.x + body.radius - 0.02f, pos.y + body.height, pos.z + body.radius - 0.02f}};
    static std::vector<int> near;
    w.query(me.min, me.max, near);
    for (int i : near)
        if (overlaps(me, w.solids[size_t(i)])) return false;
    return true;
}

bool sameBox(const AABB& a, const AABB& b) {
    return length(a.min - b.min) < 1e-3f && length(a.max - b.max) < 1e-3f;
}

}  // namespace

void Character::releaseClimb() {
    if (climb_ == Climb::Wall) {
        lastWall_ = wallBox_;
        lastWallN_ = wallN_;
        lastWallSet_ = true;
    }
    climb_ = Climb::None;
    ladder_ = -1;
    climbTime_ = 0;
    climbSpeed_ = 0;
    regrab_ = 0.3f;
}

void Character::startMount(Vec3 to, Climb after) {
    climb_ = Climb::Mount;
    mountFrom_ = body.pos;
    mountTo_ = to;
    mountAfter_ = after;
    climbTime_ = 0;
    climbSpeed_ = 0;
    body.vel = {};
    event_ = ClimbEvent::Mount;
}

// Busca algo a lo que agarrarse: una escalera (de frente o, arriba, desde el borde), un borde
// a la altura del pecho o una pared alta.
bool Character::tryGrab(float input, const World& world) {
    if (regrab_ > 0 || busy() || downTimer >= 0) return false;
    Vec3 p = body.pos;
    Vec3 want = input > 0.3f ? moveDir_ : Vec3{};
    // Hacia dónde va: lo que pide o, en el aire, su propio impulso (tras un salto de pared).
    Vec3 going = want;
    if (length(going) < 0.1f && !body.grounded && length(moveVel_) > 2.0f) going = normalize(moveVel_);

    for (size_t i = 0; i < world.ladders.size(); ++i) {
        const Ladder& l = world.ladders[i];
        Vec3 n = l.normal, side{-n.z, 0, n.x}, rel = flat(p - l.base);
        float lat = dot(rel, side), out = dot(rel, n);
        if (std::fabs(lat) > l.halfWidth + 0.55f) continue;
        // De frente: empujando hacia ella, entre el pie y lo alto.
        if (std::fabs(lat) < l.halfWidth + 0.3f && out > -0.05f && out < body.radius + 0.6f && p.y > l.base.y - 0.3f && p.y < l.top - 1.2f &&
            dot(going, -n) > 0.55f) {
            climb_ = Climb::Ladder;
            ladder_ = int(i);
            climbTime_ = 0;
            body.vel = {};
            yaw = std::atan2(-n.x, -n.z);
            event_ = ClimbEvent::Grab;
            return true;
        }
        // Desde arriba: de pie junto al borde, andando hacia el vacío por donde baja.
        bool nearTop = std::fabs(lat) < l.halfWidth + 0.55f;  // al darse la vuelta se abre en curva
        if (body.grounded && nearTop && std::fabs(p.y - l.top) < 0.3f && out > -0.9f && out < 0.25f && dot(want, n) > 0.7f) {
            ladder_ = int(i);
            yaw = std::atan2(-n.x, -n.z);
            Vec3 at = l.base + side * clampf(lat, -0.1f, 0.1f) + n * (body.radius + kLadderGap);
            at.y = l.top - 1.3f;  // por debajo de donde se encarama al subir
            startMount(at, Climb::Ladder);
            return true;
        }
    }

    // Paredes y bordes: solo en el aire, con algo de altura bajo los pies y yendo hacia ella.
    if (body.grounded || airTime_ < 0.08f || length(going) < 0.1f) return false;
    static std::vector<int> near;
    float reach = body.radius + 0.4f;
    world.query(p - Vec3{reach, 0, reach}, p + Vec3{reach, 0, reach}, near);
    for (int i : near) {
        const AABB& b = world.solids[size_t(i)];
        Vec3 n;
        float gap;
        if (!faceOf(b, p, n, gap) || gap > body.radius + 0.14f) continue;
        if (dot(going, -n) < 0.5f) continue;
        float top = b.max.y - p.y;
        // El borde a la altura del pecho: se encarama (si arriba cabe).
        if (top > 0.02f && top < 1.45f && b.min.y < p.y + 0.6f) {
            Vec3 onTop = p - n * (gap + body.radius + 0.25f);
            onTop.y = b.max.y + 0.02f;  // justo encima (en el borde exacto la física lo cuenta dentro)
            if (freeAt(world, body, onTop)) {
                yaw = std::atan2(-n.x, -n.z);
                startMount(onTop, Climb::None);
                return true;
            }
        }
        // Una pared alta: se pega, si no es la misma de la que acaba de soltarse.
        if (top < 1.3f || b.min.y > p.y + 0.5f) continue;
        if (lastWallSet_ && sameBox(b, lastWall_) && dot(n, lastWallN_) > 0.9f) continue;
        RayHit below;  // muy cerca del suelo no merece la pena: se aterriza
        if (segmentVsWorld(p + Vec3{0, 0.05f, 0}, p + Vec3{0, -0.7f, 0}, world, below)) continue;
        climb_ = Climb::Wall;
        wallBox_ = b;
        wallN_ = n;
        climbTime_ = 0;
        body.vel = {};
        doubleJumpUsed_ = false;  // desde la pared vuelve a tener el doble salto
        event_ = ClimbEvent::Cling;
        return true;
    }
    return false;
}

void Character::updateClimb(float dt, float input, bool jump, const World& world) {
    climbTime_ += dt;
    if (climb_ == Climb::Mount) {  // encaramándose: primero sube, luego entra
        float u = clampf(climbTime_ / kMountTime, 0, 1);
        float up = 1.0f - (1.0f - u) * (1.0f - u), in = smoothstep01(clampf((u - 0.3f) / 0.7f, 0, 1));
        if (mountAfter_ == Climb::Ladder) up = smoothstep01(u), in = smoothstep01(clampf(u / 0.6f, 0, 1));  // bajando: sale y baja
        Vec3 p = mountFrom_ + flat(mountTo_ - mountFrom_) * in;
        p.y = mountFrom_.y + (mountTo_.y - mountFrom_.y) * up;
        body.pos = p;
        body.vel = {};
        if (u >= 1.0f) {
            climb_ = mountAfter_;
            climbTime_ = 0;
            if (climb_ == Climb::None) {
                body.grounded = true;
                regrab_ = 0.3f;
            } else {
                body.grounded = false;
            }
        }
        return;
    }

    if (climb_ == Climb::Ladder) {
        const Ladder& l = world.ladders[size_t(ladder_)];
        Vec3 n = l.normal, side{-n.z, 0, n.x};
        yaw = std::atan2(-n.x, -n.z);
        if (jump) {  // se suelta de espaldas, con un saltito
            releaseClimb();
            yaw = std::atan2(n.x, n.z);
            moveVel_ = n * 4.0f;
            body.vel = n * 4.0f + Vec3{0, 6.5f, 0};
            body.grounded = false;
            event_ = ClimbEvent::LadderJump;
            return;
        }
        float up = input > 0.2f ? clampf(dot(moveDir_, -n) * 1.6f, -1, 1) * input : 0.0f;
        climbSpeed_ = moveTowards(climbSpeed_, up * kLadderSpeed, 18.0f * dt);
        Vec3 p = body.pos;
        p.y += climbSpeed_ * dt;
        climbPhase_ += climbSpeed_ * dt * 2.0f * kPi / kRungStep;
        // Se centra en la escalera y se arrima a la pared poco a poco.
        Vec3 rel = flat(p - l.base);
        float lat = dot(rel, side), out = dot(rel, n);
        lat = moveTowards(lat, 0, 2.5f * dt);
        out = moveTowards(out, body.radius + kLadderGap, 3.0f * dt);
        p = Vec3{l.base.x, p.y, l.base.z} + side * lat + n * out;
        body.pos = p;
        body.vel = {};
        body.grounded = false;
        if (p.y >= l.top - 1.15f && climbSpeed_ > 0) {  // arriba: se encarama al borde
            Vec3 onTop = l.base + side * lat - n * (body.radius + 0.35f);
            onTop.y = l.top + 0.02f;
            ladder_ = -1;
            startMount(onTop, Climb::None);
            return;
        }
        if (p.y <= l.base.y && climbSpeed_ < 0) {  // abajo: vuelve a pisar
            body.pos.y = l.base.y;
            releaseClimb();
            regrab_ = 0.5f;
        }
        return;
    }

    // Pegada a la pared: quieta un momento, luego resbala; al final se suelta.
    Vec3 n = wallN_;
    float face = std::atan2(n.x, n.z);  // de espaldas a la pared, mirando hacia donde saltará
    float off = wrapAngle(face - yaw);
    yaw = wrapAngle(yaw + clampf(off, -18.0f * dt, 18.0f * dt));
    if (jump) {  // salto de pared: hacia fuera y hacia arriba
        releaseClimb();
        regrab_ = 0.12f;
        yaw = face;
        moveVel_ = n * kWallJumpOut;
        body.vel = n * kWallJumpOut + Vec3{0, kWallJumpUp, 0};
        body.grounded = false;
        jumpLock_ = 0.22f;
        flipTime_ = -1;
        event_ = ClimbEvent::WallJump;
        return;
    }
    bool away = input > 0.5f && dot(moveDir_, n) > 0.6f && climbTime_ > 0.15f;
    float slide = climbTime_ < kClingHold ? 0.0f : std::min(kClingSlide, (climbTime_ - kClingHold) * 6.0f);
    body.vel = -n * 1.0f + Vec3{0, -slide + kGravity * dt, 0};  // apretada contra la pared (la física pone la gravedad)
    moveBody(body, world, dt, 0);
    bool still = body.pos.y + 0.6f < wallBox_.max.y && body.pos.y + 0.4f > wallBox_.min.y;  // aún hay pared
    if (body.grounded || away || !still || climbTime_ >= kClingTime) {
        releaseClimb();
        if (!body.grounded) body.vel = n * 1.5f;
    }
}

// Las poses de trepar: en la escalera, manos y pies alternos; en la pared, de espaldas,
// agachada y con una mano apoyada; encaramándose, las manos empujan y las rodillas suben.
void Character::animateClimb(Pose& out) const {
    Vec3* r = out.rotation;
    if (climb_ == Climb::Ladder) {
        // Cada mano (y el pie contrario) se queda en su peldaño mientras el cuerpo sube: baja
        // respecto al hombro durante el 65 % del ciclo, de estirada arriba a recogida al pecho, y
        // luego se suelta y va rápido, separándose un poco, a por el siguiente. Bajando, al revés.
        auto limb = [](float phase, float& reach) {
            float u = phase / (2.0f * kPi);
            u -= std::floor(u);
            constexpr float kHold = 0.65f;
            if (u < kHold) {
                reach = 0;
                return 1.0f - u / kHold;  // 1 = estirada arriba, 0 = recogida
            }
            float v = (u - kHold) / (1.0f - kHold);
            reach = std::sin(v * kPi);
            return smoothstep01(v);
        };
        float reachL, reachR;
        float hl = limb(climbPhase_, reachL);
        float hr = limb(climbPhase_ + kPi, reachR);
        auto arm = [](float h, float reach, Vec3& shoulder, Vec3& elbow, float side) {
            shoulder.x = lerpf(-1.55f, -2.95f, h) + 0.25f * reach;
            shoulder.z = side * (0.18f + 0.25f * reach);
            elbow.x = lerpf(-2.0f, -0.25f, h) - 0.5f * reach;
        };
        arm(hl, reachL, r[ShoulderL], r[ElbowL], 1.0f);
        arm(hr, reachR, r[ShoulderR], r[ElbowR], -1.0f);
        // El pie contrario a cada mano: arriba (rodilla recogida) cuando esa mano está abajo.
        auto leg = [](float h, float reach, Vec3& hip, Vec3& knee) {
            hip.x = lerpf(-0.25f, -1.35f, h) - 0.2f * reach;
            knee.x = lerpf(0.45f, 1.95f, h) + 0.3f * reach;
        };
        leg(hr, reachR, r[HipL], r[KneeL]);
        leg(hl, reachL, r[HipR], r[KneeR]);
        float sway = hl - hr;  // hacia el lado de la mano que está arriba
        r[Spine].x = 0.08f;
        r[Spine].z = 0.06f * sway;
        r[Chest].y = 0.12f * sway;
        r[Chest].z = 0.05f * sway;
        r[Pelvis].y = -0.08f * sway;
        r[Neck].x = -0.35f - 0.15f * std::max(reachL, reachR);
        r[Neck].y = -0.1f * sway;
        out.rootOffset.y = 0.04f * std::max(reachL, reachR);  // el tirón al subir
        return;
    }
    if (climb_ == Climb::Wall) {
        float slip = climbTime_ > kClingHold ? 1.0f : 0.0f;
        float shake = slip * 0.04f * std::sin(time_ * 38.0f);
        r[Spine].x = -0.12f;
        r[Chest].x = 0.1f;
        r[Neck].x = 0.15f;
        r[ShoulderL].x = -2.5f;  // la mano izquierda arriba, contra la pared
        r[ShoulderL].z = 0.55f;
        r[ElbowL].x = -0.7f + shake;
        r[ShoulderR].x = -0.3f;  // la otra, abierta para equilibrarse
        r[ShoulderR].z = -1.0f;
        r[ElbowR].x = -0.5f;
        r[HipL].x = -1.1f;  // una pierna recogida contra la pared, la otra más baja
        r[KneeL].x = 1.7f;
        r[HipR].x = -0.45f;
        r[KneeR].x = 0.9f + shake;
        out.rootOffset.y = -0.12f;
        return;
    }
    // Encaramándose (o saliendo hacia la escalera, al revés).
    float u = clampf(climbTime_ / kMountTime, 0, 1);
    if (mountAfter_ == Climb::Ladder) u = 1.0f - u;
    float push = smoothstep01(u);
    r[ShoulderL].x = r[ShoulderR].x = lerpf(-2.6f, -0.5f, push);
    r[ShoulderL].z = 0.3f;
    r[ShoulderR].z = -0.3f;
    r[ElbowL].x = r[ElbowR].x = lerpf(-0.5f, -0.2f, push);
    r[Spine].x = lerpf(0.1f, 0.55f, std::sin(u * kPi));
    r[HipL].x = lerpf(-0.6f, -1.7f, std::sin(u * kPi));
    r[KneeL].x = lerpf(1.0f, 2.1f, std::sin(u * kPi));
    r[HipR].x = lerpf(-0.3f, -0.9f, push);
    r[KneeR].x = lerpf(0.6f, 1.3f, push);
    r[Neck].x = -0.2f;
    out.rootOffset.y = -0.1f * std::sin(u * kPi);
}
