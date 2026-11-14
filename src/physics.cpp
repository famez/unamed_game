#include "physics.h"

#include <utility>

namespace {

float axisOf(const Vec3& v, int axis) { return axis == 0 ? v.x : (axis == 1 ? v.y : v.z); }
float& axisOf(Vec3& v, int axis) { return axis == 0 ? v.x : (axis == 1 ? v.y : v.z); }

// Saca el cuerpo de las cajas que solape en el eje horizontal `axis` (0 = X, 2 = Z).
void resolveHorizontal(Body& b, const World& w, int axis, bool wasGrounded, float stepHeight) {
    for (const AABB& box : w.solids) {
        if (!overlaps(b.bounds(), box)) continue;

        // Bordillo bajo: en vez de chocar, subimos encima.
        float rise = box.max.y - b.pos.y;
        if (wasGrounded && rise > 0 && rise <= stepHeight) {
            b.pos.y = box.max.y;
            continue;
        }
        // Salimos por el lado del que venimos (el del centro del cuerpo).
        float center = axisOf(box.center(), axis);
        float& p = axisOf(b.pos, axis);
        p = p < center ? axisOf(box.min, axis) - b.radius : axisOf(box.max, axis) + b.radius;
        axisOf(b.vel, axis) = 0;
    }
}

}  // namespace

void moveBody(Body& b, const World& w, float dt, float stepHeight) {
    bool wasGrounded = b.grounded;
    b.vel.y -= kGravity * dt;

    b.pos.x += b.vel.x * dt;
    resolveHorizontal(b, w, 0, wasGrounded, stepHeight);
    b.pos.z += b.vel.z * dt;
    resolveHorizontal(b, w, 2, wasGrounded, stepHeight);

    b.grounded = false;
    b.pos.y += b.vel.y * dt;
    for (const AABB& box : w.solids) {
        if (!overlaps(b.bounds(), box)) continue;
        if (b.vel.y <= 0) {  // cayendo: aterriza encima
            b.pos.y = box.max.y;
            b.grounded = true;
        } else {             // subiendo: se da con la cabeza
            b.pos.y = box.min.y - b.height;
        }
        b.vel.y = 0;
    }
    if (w.ground && b.pos.y <= 0) {
        b.pos.y = 0;
        b.vel.y = 0;
        b.grounded = true;
    }
}

bool segmentVsAABB(Vec3 p0, Vec3 p1, const AABB& box, RayHit& hit) {
    Vec3 d = p1 - p0;
    float tMin = 0, tMax = 1;
    int hitAxis = -1;
    float hitSign = 0;
    for (int axis = 0; axis < 3; ++axis) {
        float o = axisOf(p0, axis), dir = axisOf(d, axis);
        float lo = axisOf(box.min, axis), hi = axisOf(box.max, axis);
        if (std::fabs(dir) < 1e-8f) {
            if (o < lo || o > hi) return false;  // paralelo y fuera de la franja
            continue;
        }
        float t1 = (lo - o) / dir, t2 = (hi - o) / dir;
        float sign = -1;  // entra por la cara mínima → normal hacia -eje
        if (t1 > t2) {
            std::swap(t1, t2);
            sign = 1;
        }
        if (t1 > tMin) {
            tMin = t1;
            hitAxis = axis;
            hitSign = sign;
        }
        tMax = std::fmin(tMax, t2);
        if (tMin > tMax) return false;
    }
    hit.t = tMin;
    hit.normal = {};
    if (hitAxis >= 0)
        axisOf(hit.normal, hitAxis) = hitSign;
    else
        hit.normal = {0, 1, 0};  // el segmento empieza dentro de la caja
    return true;
}

bool segmentVsWorld(Vec3 p0, Vec3 p1, const World& w, RayHit& hit) {
    bool found = false;
    hit.t = 2;
    if (w.ground && p1.y < 0 && p0.y >= 0) {
        hit.t = p0.y / (p0.y - p1.y);
        hit.normal = {0, 1, 0};
        found = true;
    }
    RayHit h;
    for (const AABB& box : w.solids)
        if (segmentVsAABB(p0, p1, box, h) && h.t < hit.t) {
            hit = h;
            found = true;
        }
    return found;
}
