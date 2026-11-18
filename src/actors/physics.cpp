#include "actors/physics.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace {

float axisOf(const Vec3& v, int axis) { return axis == 0 ? v.x : (axis == 1 ? v.y : v.z); }
float& axisOf(Vec3& v, int axis) { return axis == 0 ? v.x : (axis == 1 ? v.y : v.z); }

// Cajas cerca del cuerpo (consulta de la rejilla, reutilizando la memoria).
const std::vector<int>& near(const World& w, Vec3 lo, Vec3 hi) {
    thread_local std::vector<int> found;
    w.query(lo, hi, found);
    return found;
}

// Saca el cuerpo de las cajas que solape en el eje horizontal `axis` (0 = X, 2 = Z).
void resolveHorizontal(Body& b, const World& w, int axis, bool wasGrounded, float stepHeight,
                       const std::vector<int>& candidates) {
    for (int i : candidates) {
        const AABB& box = w.solids[size_t(i)];
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

float Terrain::heightAt(float x, float z) const {
    if (nx < 2 || nz < 2) return 0;
    float fx = std::clamp((x - x0) / cell, 0.0f, float(nx - 1) - 1e-4f);
    float fz = std::clamp((z - z0) / cell, 0.0f, float(nz - 1) - 1e-4f);
    int ix = int(fx), iz = int(fz);
    float u = fx - float(ix), v = fz - float(iz);
    // Dos triángulos por celda (como la malla que se dibuja): el suelo coincide con lo que se ve.
    float h00 = at(ix, iz), h10 = at(ix + 1, iz), h01 = at(ix, iz + 1), h11 = at(ix + 1, iz + 1);
    if (u + v <= 1) return h00 + (h10 - h00) * u + (h01 - h00) * v;
    return h11 + (h01 - h11) * (1 - u) + (h10 - h11) * (1 - v);
}

float Terrain::fenceAt(float x, float z) const {
    if (fence.empty() || nx < 2 || nz < 2) return -1e9f;
    float fx = std::clamp((x - x0) / cell, 0.0f, float(nx - 1) - 1e-4f);
    float fz = std::clamp((z - z0) / cell, 0.0f, float(nz - 1) - 1e-4f);
    int ix = int(fx), iz = int(fz);
    float u = fx - float(ix), v = fz - float(iz);
    auto f = [&](int i, int k) { return fence[size_t(k) * size_t(nx) + size_t(i)]; };
    return (f(ix, iz) * (1 - u) + f(ix + 1, iz) * u) * (1 - v) + (f(ix, iz + 1) * (1 - u) + f(ix + 1, iz + 1) * u) * v;
}

Vec3 Terrain::normalAt(float x, float z) const {
    float e = cell * 0.5f;
    float dx = heightAt(x + e, z) - heightAt(x - e, z), dz = heightAt(x, z + e) - heightAt(x, z - e);
    return normalize(Vec3{-dx, 2 * e, -dz});
}

void World::buildGrid() {
    grid = SolidGrid{};
    grid.count = solids.size();
    if (solids.size() < 64) return;  // pocas: se miran todas
    Vec3 lo{1e9f, 0, 1e9f}, hi{-1e9f, 0, -1e9f};
    for (const AABB& b : solids) {
        lo.x = std::min(lo.x, b.min.x), lo.z = std::min(lo.z, b.min.z);
        hi.x = std::max(hi.x, b.max.x), hi.z = std::max(hi.z, b.max.z);
    }
    grid.x0 = lo.x, grid.z0 = lo.z;
    grid.nx = std::max(1, int((hi.x - lo.x) / grid.cell) + 1);
    grid.nz = std::max(1, int((hi.z - lo.z) / grid.cell) + 1);
    grid.cells.assign(size_t(grid.nx) * size_t(grid.nz), {});
    for (size_t i = 0; i < solids.size(); ++i) {
        const AABB& b = solids[i];
        int ax = std::clamp(int((b.min.x - grid.x0) / grid.cell), 0, grid.nx - 1);
        int bx = std::clamp(int((b.max.x - grid.x0) / grid.cell), 0, grid.nx - 1);
        int az = std::clamp(int((b.min.z - grid.z0) / grid.cell), 0, grid.nz - 1);
        int bz = std::clamp(int((b.max.z - grid.z0) / grid.cell), 0, grid.nz - 1);
        for (int z = az; z <= bz; ++z)
            for (int x = ax; x <= bx; ++x) grid.cells[size_t(z) * size_t(grid.nx) + size_t(x)].push_back(int(i));
    }
}

void World::query(Vec3 lo, Vec3 hi, std::vector<int>& out) const {
    out.clear();
    if (grid.cells.empty() || grid.count != solids.size()) {  // sin rejilla (o anticuada): todas
        for (size_t i = 0; i < solids.size(); ++i) out.push_back(int(i));
        return;
    }
    int ax = int(std::floor((lo.x - grid.x0) / grid.cell)), bx = int(std::floor((hi.x - grid.x0) / grid.cell));
    int az = int(std::floor((lo.z - grid.z0) / grid.cell)), bz = int(std::floor((hi.z - grid.z0) / grid.cell));
    ax = std::max(ax, 0), az = std::max(az, 0), bx = std::min(bx, grid.nx - 1), bz = std::min(bz, grid.nz - 1);
    for (int z = az; z <= bz; ++z)
        for (int x = ax; x <= bx; ++x) {
            const std::vector<int>& c = grid.cells[size_t(z) * size_t(grid.nx) + size_t(x)];
            out.insert(out.end(), c.begin(), c.end());
        }
    if (ax != bx || az != bz) {  // una caja en varias celdas saldría repetida
        std::sort(out.begin(), out.end());
        out.erase(std::unique(out.begin(), out.end()), out.end());
    }
}

void moveBody(Body& b, const World& w, float dt, float stepHeight) {
    bool wasGrounded = b.grounded;
    b.vel.y -= kGravity * dt;
    // Las cajas que puede tocar en este paso (con margen por lo que se mueve).
    float reach = b.radius + (std::fabs(b.vel.x) + std::fabs(b.vel.z)) * dt + 0.5f;
    const std::vector<int>& candidates = near(w, b.pos - Vec3{reach, 0, reach}, b.pos + Vec3{reach, 0, reach});

    // El recinto del mapa: no deja salir (sí volver hacia dentro si algo lo ha sacado).
    auto fenced = [&](float& axisPos, float& axisVel, float before) {
        if (!w.terrain || w.terrain->fence.empty()) return;
        float now = axisPos;
        float d = w.terrain->fenceAt(b.pos.x, b.pos.z);
        axisPos = before;
        float d0 = w.terrain->fenceAt(b.pos.x, b.pos.z);
        axisPos = now;
        if (d + b.radius > 0 && d > d0) axisPos = before, axisVel = 0;
    };
    float x0 = b.pos.x;
    b.pos.x += b.vel.x * dt;
    resolveHorizontal(b, w, 0, wasGrounded, stepHeight, candidates);
    fenced(b.pos.x, b.vel.x, x0);
    float z0 = b.pos.z;
    b.pos.z += b.vel.z * dt;
    resolveHorizontal(b, w, 2, wasGrounded, stepHeight, candidates);
    fenced(b.pos.z, b.vel.z, z0);

    b.grounded = false;
    b.pos.y += b.vel.y * dt;
    for (int i : candidates) {
        const AABB& box = w.solids[size_t(i)];
        if (!overlaps(b.bounds(), box)) continue;
        if (b.vel.y <= 0) {  // cayendo: aterriza encima
            b.pos.y = box.max.y;
            b.grounded = true;
        } else {             // subiendo: se da con la cabeza
            b.pos.y = box.min.y - b.height;
        }
        b.vel.y = 0;
    }
    if (w.ground) {
        float g = w.groundAt(b.pos.x, b.pos.z);
        // Bajando una cuesta andando: sigue pegado al suelo en lugar de ir a saltitos.
        bool snap = w.terrain && wasGrounded && !b.grounded && b.vel.y <= 0 && b.pos.y - g < stepHeight + 0.05f;
        if (b.pos.y <= g || snap) {
            b.pos.y = g;
            b.vel.y = 0;
            b.grounded = true;
        }
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
    if (w.ground && w.terrain) {  // relieve: se recorre el segmento y se afina el cruce
        const Terrain& t = *w.terrain;
        auto above = [&](float u) {
            Vec3 p = p0 + (p1 - p0) * u;
            return p.y - t.heightAt(p.x, p.z);
        };
        float len = length(p1 - p0);
        int steps = std::clamp(int(len / (t.cell * 0.5f)) + 1, 1, 512);
        float prevU = 0;
        if (above(0) >= 0)
            for (int i = 1; i <= steps; ++i) {
                float u = float(i) / float(steps), a = above(u);
                if (a < 0) {
                    float lo = prevU, hi = u;
                    for (int k = 0; k < 10; ++k) {
                        float mid = (lo + hi) * 0.5f;
                        (above(mid) < 0 ? hi : lo) = mid;
                    }
                    Vec3 p = p0 + (p1 - p0) * hi;
                    hit.t = hi;
                    hit.normal = t.normalAt(p.x, p.z);
                    found = true;
                    break;
                }
                prevU = u;
            }
    } else if (w.ground && p1.y < 0 && p0.y >= 0) {
        hit.t = p0.y / (p0.y - p1.y);
        hit.normal = {0, 1, 0};
        found = true;
    }
    RayHit h;
    Vec3 lo{std::min(p0.x, p1.x), 0, std::min(p0.z, p1.z)}, hi{std::max(p0.x, p1.x), 0, std::max(p0.z, p1.z)};
    for (int i : near(w, lo, hi)) {
        const AABB& box = w.solids[size_t(i)];
        if (segmentVsAABB(p0, p1, box, h) && h.t < hit.t) {
            hit = h;
            found = true;
        }
    }
    return found;
}
