#include "game.h"

#include "renderer.h"
#include "models.h"
#include "text.h"

#include <cstdio>

namespace {

uint32_t withAlpha(uint32_t c, float a) { return (c & 0x00FFFFFFu) | uint32_t(clampf(a, 0, 1) * 255.0f + 0.5f) << 24; }
uint32_t scaleRgb(uint32_t c, float k) {
    auto ch = [&](int shift) { return uint32_t(clampf(float((c >> shift) & 0xFF) * k, 0, 255)) << shift; };
    return ch(0) | ch(8) | ch(16) | (c & 0xFF000000u);
}
uint32_t mixRgb(uint32_t a, uint32_t b, float t) {
    auto ch = [&](int shift) {
        return uint32_t(lerpf(float((a >> shift) & 0xFF), float((b >> shift) & 0xFF), t)) << shift;
    };
    return ch(0) | ch(8) | ch(16) | (a & 0xFF000000u);
}

// Base ortonormal con el eje Y local apuntando a `d` (para orientar pinchos y rayos).
Mat4 basisY(Vec3 d) {
    Vec3 y = normalize(d);
    Vec3 ref = std::fabs(y.y) < 0.9f ? Vec3{0, 1, 0} : Vec3{1, 0, 0};
    Vec3 x = normalize(cross(ref, y)), z = cross(x, y);
    Mat4 m = Mat4::identity();
    m.m[0] = x.x; m.m[1] = x.y; m.m[2] = x.z;
    m.m[4] = y.x; m.m[5] = y.y; m.m[6] = y.z;
    m.m[8] = z.x; m.m[9] = z.y; m.m[10] = z.z;
    return m;
}

// --- Efectos aditivos (capa `glow`): cuadrados orientados a cámara ---

void glowQuad(DebugDraw& g, Vec3 p, Vec3 ax, Vec3 ay, uint32_t c) {
    g.quad(p - ax - ay, p + ax - ay, p + ax + ay, p - ax + ay, c);
}
// Halo redondo aproximado (octógono con el centro más intenso).
void glowDot(DebugDraw& g, const ViewInfo& v, Vec3 p, float size, uint32_t c) {
    const int n = 8;
    for (int i = 0; i < n; ++i) {
        float a0 = 2.0f * kPi * float(i) / n, a1 = 2.0f * kPi * float(i + 1) / n;
        Vec3 p0 = p + (v.right * std::cos(a0) + v.up * std::sin(a0)) * size;
        Vec3 p1 = p + (v.right * std::cos(a1) + v.up * std::sin(a1)) * size;
        g.triangle(p, p0, p1, c);
    }
}
// Destello de 4 puntas que gira (el brillo típico de los golpes y del botín).
void glowStar(DebugDraw& g, const ViewInfo& v, Vec3 p, float size, float angle, uint32_t c) {
    Vec3 a = v.right * std::cos(angle) + v.up * std::sin(angle);
    Vec3 b = v.right * -std::sin(angle) + v.up * std::cos(angle);
    glowQuad(g, p, a * size, b * (size * 0.18f), c);
    glowQuad(g, p, a * (size * 0.18f), b * size, c);
    glowDot(g, v, p, size * 0.3f, c);
}
// Anillo plano en el suelo (onda expansiva).
void glowRing(DebugDraw& g, Vec3 c, float r0, float r1, uint32_t col) {
    const int n = 40;
    for (int i = 0; i < n; ++i) {
        float a0 = 2.0f * kPi * float(i) / n, a1 = 2.0f * kPi * float(i + 1) / n;
        Vec3 d0{std::cos(a0), 0, std::sin(a0)}, d1{std::cos(a1), 0, std::sin(a1)};
        g.quad(c + d0 * r0, c + d1 * r0, c + d1 * r1, c + d0 * r1, col);
    }
}

// --- Protagonista: maniquí de elipsoides con proporciones de dibujo animado ---

struct Shape {
    int joint;
    Vec3 center, radii;
    uint32_t color;
    Material material;
};
// Detalles rígidos del Prismero (el cuerpo es la malla de models.cpp): lo que debe verse
// nítido o brillar.
const uint32_t kHair = rgba(232, 78, 176), kSuitC = rgba(240, 244, 252), kNeon = rgba(40, 215, 255);
const uint32_t kNeonPink = rgba(255, 80, 190), kEye = rgba(24, 58, 112), kOutline = rgba(28, 24, 58);
// clang-format off
const Shape kHeroShapes[] = {
    // ojos grandes con iris luminoso y brillo, cejas y boca
    {Head,      {0.055f, 0.14f, 0.143f},  {0.031f, 0.043f, 0.016f}, kEye, Material::Cloth},
    {Head,      {-0.055f, 0.14f, 0.143f}, {0.031f, 0.043f, 0.016f}, kEye, Material::Cloth},
    {Head,      {0.055f, 0.134f, 0.155f}, {0.019f, 0.026f, 0.008f}, kNeon, Material::Glow},
    {Head,      {-0.055f, 0.134f, 0.155f},{0.019f, 0.026f, 0.008f}, kNeon, Material::Glow},
    {Head,      {0.064f, 0.153f, 0.16f},  {0.008f, 0.011f, 0.004f}, rgba(255, 255, 255), Material::Glow},
    {Head,      {-0.046f, 0.153f, 0.16f}, {0.008f, 0.011f, 0.004f}, rgba(255, 255, 255), Material::Glow},
    {Head,      {0.058f, 0.195f, 0.142f}, {0.032f, 0.008f, 0.012f}, kHair, Material::Cloth},
    {Head,      {-0.058f, 0.195f, 0.142f},{0.032f, 0.008f, 0.012f}, kHair, Material::Cloth},
    {Head,      {0, 0.068f, 0.138f},      {0.018f, 0.005f, 0.008f}, rgba(200, 90, 110), Material::Cloth},
    // auriculares con anillo de neón
    {Head,      {0.158f, 0.14f, 0},       {0.03f, 0.052f, 0.052f},  kSuitC, Material::Metal},
    {Head,      {-0.158f, 0.14f, 0},      {0.03f, 0.052f, 0.052f},  kSuitC, Material::Metal},
    {Head,      {0.186f, 0.14f, 0},       {0.007f, 0.036f, 0.036f}, kNeon, Material::Glow},
    {Head,      {-0.186f, 0.14f, 0},      {0.007f, 0.036f, 0.036f}, kNeon, Material::Glow},
};
// Líneas de neón del traje (cajas finas sobre la superficie).
struct NeonLine {
    int joint;
    Vec3 center, half;
    uint32_t color;
};
const NeonLine kNeonLines[] = {
    {Chest,  {0.07f, 0.08f, 0.124f},  {0.005f, 0.09f, 0.004f}, kNeon},
    {Chest,  {-0.07f, 0.08f, 0.124f}, {0.005f, 0.09f, 0.004f}, kNeon},
    {ElbowL, {0, -0.11f, 0.044f},     {0.006f, 0.08f, 0.004f}, kNeon},
    {ElbowR, {0, -0.11f, 0.044f},     {0.006f, 0.08f, 0.004f}, kNeon},
    {HipL,   {0, -0.13f, 0.111f},     {0.006f, 0.12f, 0.004f}, kNeonPink},
    {HipR,   {0, -0.13f, 0.111f},     {0.006f, 0.12f, 0.004f}, kNeonPink},
    {AnkleL, {0, -0.074f, 0.065f},    {0.082f, 0.006f, 0.162f}, kNeon},
    {AnkleR, {0, -0.074f, 0.065f},    {0.082f, 0.006f, 0.162f}, kNeon},
    {Pelvis, {0, 0.06f, 0.119f},      {0.03f, 0.018f, 0.006f}, kNeonPink},
};
// clang-format on
// Pelo de punta: direcciones desde el centro de la cabeza.
const Vec3 kSpikes[] = {
    {0, 0.35f, -1}, {0.6f, 0.4f, -0.8f}, {-0.6f, 0.4f, -0.8f}, {0.9f, 0.5f, -0.2f}, {-0.9f, 0.5f, -0.2f},
    {0.3f, 1, -0.4f}, {-0.35f, 1, -0.3f}, {0, -0.1f, -1}, {0.5f, 0, -0.9f}, {-0.5f, 0, -0.9f},
};
// clang-format on

void drawSkeleton(DebugDraw& dd, const JointDef* rig, int count, const Mat4* joints, const DrawFlags& f,
                  uint32_t boxColor) {
    for (int i = 0; i < count; ++i) {
        const JointDef& j = rig[i];
        Vec3 p = translationOf(joints[i]);
        if (f.bones && j.parent >= 0) {
            // Convención de herramientas 3D: izquierda (+X local) azul, derecha roja.
            float side = j.offset.x;
            for (int k = j.parent; k >= 0 && side == 0; k = rig[k].parent) side = rig[k].offset.x;
            uint32_t col = side > 0 ? rgba(90, 160, 255) : side < 0 ? rgba(255, 110, 90) : rgba(255, 220, 80);
            dd.line(translationOf(joints[j.parent]), p, col);
        }
        if (f.joints) dd.point(p, rgba(255, 255, 255));
        bool hasBox = j.boxHalf.x > 0 || j.boxHalf.y > 0 || j.boxHalf.z > 0;
        if (f.body && hasBox) dd.box(joints[i], j.boxCenter, j.boxHalf, boxColor);
    }
}

void drawGrid(DebugDraw& dd) {
    const int half = 25;
    for (int i = -half; i <= half; ++i) {
        uint32_t col = (i % 5 == 0) ? rgba(80, 85, 100) : rgba(45, 48, 56);
        dd.line({float(i), 0, float(-half)}, {float(i), 0, float(half)}, col);
        dd.line({float(-half), 0, float(i)}, {float(half), 0, float(i)}, col);
    }
}

// Arma en la mano: la hoja sale de la muñeca a lo largo de +Z local.
void drawWeapon(Renderer& r, const Mat4& w, const WeaponStats& ws, float time) {
    float len = ws.length;
    Vec3 base = kBladeBase, tip = kBladeBase + Vec3{0, 0, len};
    Vec3 mid = (base + tip) * 0.5f;
    switch (ws.style) {
        case WeaponStyle::Sword:
            r.box(w, kGripLocal, {0.02f, 0.02f, 0.075f}, ws.hilt, Material::Cloth);
            r.box(w, kGripLocal + Vec3{0, 0, 0.085f}, {0.085f, 0.022f, 0.022f}, scaleRgb(ws.hilt, 1.3f), Material::Cloth);
            r.box(w, mid, {0.036f, 0.013f, len * 0.5f}, ws.blade, Material::Cloth);
            r.ellipsoid(w, tip, {0.036f, 0.013f, 0.06f}, ws.blade, Material::Cloth);
            break;
        case WeaponStyle::Staff: {
            r.box(w, mid - Vec3{0, 0, 0.12f}, {0.02f, 0.02f, len * 0.5f + 0.12f}, ws.blade, Material::Metal);
            r.ellipsoid(w, base, {0.035f, 0.035f, 0.03f}, ws.hilt, Material::Metal);
            // Estrella de cinco puntas que gira en la punta.
            Mat4 star = w * translate(tip + Vec3{0, 0, 0.06f}) * rotateZ(time * 2.0f);
            r.ellipsoid(star, {}, {0.05f, 0.05f, 0.03f}, ws.hilt, Material::Glow);
            for (int i = 0; i < 5; ++i) {
                float a = 2.0f * kPi * float(i) / 5.0f;
                Vec3 d{std::cos(a), std::sin(a), 0};
                r.ellipsoid(star * translate(d * 0.07f) * basisY(d), {}, {0.024f, 0.06f, 0.016f}, ws.hilt, Material::Glow);
            }
            break;
        }
        case WeaponStyle::Saber:
            r.box(w, kGripLocal, {0.02f, 0.02f, 0.075f}, ws.hilt, Material::Metal);
            r.ellipsoid(w, kGripLocal + Vec3{0, 0, 0.09f}, {0.11f, 0.03f, 0.04f}, ws.trail, Material::Glow);
            r.box(w, mid, {0.042f, 0.011f, len * 0.5f}, ws.blade, Material::Metal);
            r.box(w, mid, {0.012f, 0.014f, len * 0.48f}, ws.trail, Material::Glow);
            r.ellipsoid(w, tip, {0.045f, 0.02f, 0.09f}, ws.trail, Material::Glow);
            break;
    }
}

// --- HUD: primitivas 2D en píxeles (origen arriba a la izquierda) ---

void fillRect(DebugDraw& hud, float x0, float y0, float x1, float y1, uint32_t c) {
    hud.quad({x0, y0, 0}, {x1, y0, 0}, {x1, y1, 0}, {x0, y1, 0}, c);
}
void fillCircle(DebugDraw& hud, float cx, float cy, float r, uint32_t c, int n = 28) {
    for (int i = 0; i < n; ++i) {
        float a0 = 2.0f * kPi * float(i) / float(n), a1 = 2.0f * kPi * float(i + 1) / float(n);
        hud.triangle({cx, cy, 0}, {cx + std::cos(a0) * r, cy + std::sin(a0) * r, 0},
                     {cx + std::cos(a1) * r, cy + std::sin(a1) * r, 0}, c);
    }
}
// Barra con extremos redondeados: fondo, "fantasma" (lo que se acaba de perder) y relleno.
void roundBar(DebugDraw& hud, float xRight, float y, float length, float thick, float frac, float ghost,
              uint32_t fill, uint32_t ghostCol) {
    float r = thick * 0.5f, x0 = xRight - length;
    fillRect(hud, x0 - 3, y - 3, xRight + 3, y + thick + 3, rgba(20, 24, 40, 220));
    fillCircle(hud, x0 - 3 + r, y + r, r + 3, rgba(20, 24, 40, 220), 14);
    auto seg = [&](float a, float b, uint32_t c) {
        if (b <= a) return;
        fillRect(hud, x0 + length * a, y, x0 + length * b, y + thick, c);
    };
    seg(0, 1, rgba(50, 40, 60));
    seg(frac, ghost, ghostCol);
    seg(0, frac, fill);
    // Brillo en la mitad de arriba.
    if (frac > 0) fillRect(hud, x0, y + 2, x0 + length * frac, y + thick * 0.4f, withAlpha(rgba(255, 255, 255), 0.35f));
}

void hudText(DebugDraw& hud, float x, float y, float scale, const char* s, uint32_t c, bool shadow = true) {
    if (shadow) drawText(hud, x + scale * 0.6f, y + scale * 0.6f, scale, s, rgba(10, 12, 30, 200));
    drawText(hud, x, y, scale, s, c);
}

bool project(const ViewInfo& v, Vec3 p, float& sx, float& sy) {
    const float* m = v.viewProj.m;
    float cx = m[0] * p.x + m[4] * p.y + m[8] * p.z + m[12];
    float cy = m[1] * p.x + m[5] * p.y + m[9] * p.z + m[13];
    float cw = m[3] * p.x + m[7] * p.y + m[11] * p.z + m[15];
    if (cw < 0.1f) return false;
    sx = (cx / cw * 0.5f + 0.5f) * float(v.width);
    sy = (1.0f - (cy / cw * 0.5f + 0.5f)) * float(v.height);
    return true;
}

}  // namespace

// --- Escena -------------------------------------------------------------------------------

void Game::draw(DebugDraw& dd, DebugDraw& glow, Renderer* solid, const DrawFlags& f, const ViewInfo& view,
                float alpha) const {
    drawLevel(dd, solid, f, view, alpha);
    drawHero(dd, solid, f, alpha);
    drawEnemies(dd, glow, solid, f, view, alpha);
    drawEffects(dd, glow, solid, view);
}

void Game::drawLevel(DebugDraw& dd, Renderer* solid, const DrawFlags& f, const ViewInfo& view, float alpha) const {
    Mat4 id = Mat4::identity();
    if (!solid) {
        // Modo debug: solo alambre (escenario, cajas, cofres) y la cuadrícula.
        if (f.grid) drawGrid(dd);
        for (const Block& b : blocks_) dd.box(id, b.box.center(), b.box.halfExtents(), rgba(120, 110, 90));
        for (const Crate& c : crates_)
            if (!c.broken) dd.box(id, c.box.center(), c.box.halfExtents(), rgba(200, 150, 80));
        for (const TreasureChest& c : chests_)
            dd.box(translate(c.pos) * rotateY(c.yaw), {0, 0.35f, 0}, {0.45f, 0.35f, 0.3f},
                   c.open ? rgba(120, 110, 60) : rgba(255, 220, 80));
        return;
    }
    // Césped alrededor y plaza de adoquines.
    solid->box(id, {0, -0.5f, 0}, {200, 0.5f, 200}, rgba(80, 200, 140), Material::Grass);
    solid->box(id, {0, 0.004f, 0}, {16.2f, 0.004f, 16.2f}, rgba(205, 210, 226), Material::Ground);
    // Prisma de la fuente: flota, gira y proyecta luz cambiante.
    {
        float bob = 0.12f * std::sin(time_ * 1.3f);
        Mat4 m = translate({0, 3.0f + bob, 0}) * rotateY(time_ * 0.8f);
        solid->box(m * rotateX(kPi * 0.25f) * rotateZ(kPi * 0.25f), {}, {0.32f, 0.32f, 0.32f}, rgba(255, 120, 220), Material::Glow);
        for (int i = 0; i < 3; ++i) {  // anillos que orbitan
            Mat4 r = translate({0, 3.0f + bob, 0}) * rotateY(time_ * (0.9f + 0.4f * float(i)) + float(i)) *
                     rotateX(0.5f + 0.4f * float(i));
            for (int k = 0; k < 14; ++k) {
                float a = 2.0f * kPi * float(k) / 14.0f;
                solid->box(r * rotateY(a), {0.75f + 0.12f * float(i), 0, 0}, {0.03f, 0.03f, 0.13f},
                           i == 1 ? rgba(40, 220, 255) : rgba(255, 190, 60), Material::Glow);
            }
        }
        Vec3 hue{0.6f + 0.4f * std::sin(time_), 0.5f + 0.4f * std::sin(time_ + 2.1f), 0.8f + 0.2f * std::sin(time_ + 4.2f)};
        solid->light({0, 3.0f, 0}, hue * 2.5f, 7.0f);
    }
    // Objetos estrechos (farolas, troncos, columnas) que tapan al personaje: no se dibujan,
    // ni la decoración que llevan encima (copa, lámpara).
    Vec3 heroChest = hero_.interpolatedPos(alpha) + Vec3{0, 1.0f, 0};
    std::vector<AABB> hidden;
    for (const Block& b : blocks_) {
        Vec3 h = b.box.halfExtents();
        if (h.x > 0.6f || h.z > 0.6f) continue;
        AABB grown{b.box.min - Vec3{0.25f, 0, 0.25f}, b.box.max + Vec3{0.25f, 3.0f, 0.25f}};
        RayHit hit;
        if (segmentVsAABB(view.eye, heroChest, grown, hit)) hidden.push_back(b.box);
    }
    auto isHidden = [&](Vec3 p) {
        for (const AABB& a : hidden)
            if (std::fabs(p.x - a.center().x) < 1.8f && std::fabs(p.z - a.center().z) < 1.8f && p.y > a.min.y) return true;
        return false;
    };
    for (const Block& b : blocks_)
        if (!isHidden(b.box.center())) solid->box(id, b.box.center(), b.box.halfExtents(), b.color, b.material);
    for (const Decor& d : decor_) {
        if (!hidden.empty() && isHidden(translationOf(d.transform) + d.center)) continue;
        if (d.sphere)
            solid->ellipsoid(d.transform, d.center, d.half, d.color, d.material);
        else
            solid->box(d.transform, d.center, d.half, d.color, d.material);
    }
    // Solo las farolas cercanas: cada luz cuesta en todos los píxeles de la escena.
    for (size_t i = 1; i < lamps_.size(); ++i)
        if (length(flat(lamps_[i] - hero_.body.pos)) < 14.0f) solid->light(lamps_[i], Vec3{0.3f, 0.85f, 1.0f} * 1.3f, 5.0f);

    for (const Crate& c : crates_) {
        if (c.broken) continue;
        Vec3 ctr = c.box.center(), h = c.box.halfExtents();
        float flash = c.hitFlash > 0 ? 0.6f : 0;
        // Célula de energía: caja blanca con franjas de luz que laten.
        solid->box(id, ctr, h, rgba(240, 242, 250), Material::Stone, flash);
        uint32_t band = rgba(255, 190, 60);
        for (float s : {-1.0f, 1.0f}) {
            solid->box(id, ctr + Vec3{0, 0, s * (h.z + 0.005f)}, {h.x * 0.8f, 0.05f, 0.005f}, band, Material::Glow, flash);
            solid->box(id, ctr + Vec3{s * (h.x + 0.005f), 0, 0}, {0.005f, 0.05f, h.z * 0.8f}, band, Material::Glow, flash);
        }
    }
    // Cofres: caja con remaches dorados; la tapa gira sobre la bisagra de atrás al abrirse.
    for (const TreasureChest& c : chests_) {
        Mat4 m = translate(c.pos) * rotateY(c.yaw);
        const uint32_t wood = rgba(244, 246, 252), gold = rgba(255, 70, 190);
        solid->box(m, {0, 0.22f, 0}, {0.45f, 0.22f, 0.3f}, wood, Material::Cloth);
        solid->box(m, {0, 0.22f, 0.302f}, {0.47f, 0.04f, 0.004f}, gold, Material::Glow);
        float open = c.open ? smoothstep01(c.openTime / 0.4f) : 0.0f;
        Mat4 lid = m * translate({0, 0.44f, -0.3f}) * rotateX(-open * 1.9f);
        solid->box(lid, {0, 0.1f, 0.3f}, {0.46f, 0.1f, 0.31f}, wood, Material::Cloth);
        solid->box(lid, {0, 0.1f, 0.3f}, {0.08f, 0.105f, 0.315f}, gold, Material::Glow);
        solid->box(lid, {0, 0.06f, 0.615f}, {0.06f, 0.06f, 0.01f}, rgba(40, 220, 255), Material::Glow);  // cerradura
        bool near = length(flat(c.pos - hero_.body.pos)) < 12.0f;
        if (!c.open && near) {
            float pulse = 0.6f + 0.4f * std::sin(time_ * 3.0f);
            solid->light(c.pos + Vec3{0, 0.9f, 0}, Vec3{1.0f, 0.85f, 0.4f} * pulse, 2.5f);
        } else if (c.open && c.openTime < 1.5f) {
            solid->light(c.pos + Vec3{0, 0.8f, 0}, Vec3{1.0f, 0.9f, 0.5f} * (3.0f * (1.0f - c.openTime / 1.5f)), 4.0f);
        }
    }
}

void Game::drawHero(DebugDraw& dd, Renderer* solid, const DrawFlags& f, float alpha) const {
    Mat4 j[JointCount];
    hero_.interpolatedJoints(alpha, j);
    const WeaponStats& ws = inv_.weaponStats();
    bool blink = heroInvuln_ > 0 && heroFlash_ <= 0 && hero_.downTimer < 0 && std::fmod(time_, 0.16f) < 0.05f;
    if (!solid) {
        uint32_t col = heroFlash_ > 0 ? rgba(255, 255, 255) : rgba(80, 220, 200);
        drawSkeleton(dd, kSkeleton, JointCount, j, f, col);
        dd.line(transformPoint(j[WristR], kGripLocal),
                transformPoint(j[WristR], kBladeBase + Vec3{0, 0, ws.length}), rgba(255, 240, 150));  // arma
        Vec3 rib[Character::kRibbonPoints];
        hero_.ribbon(alpha, rib);
        for (int i = 0; i + 1 < Character::kRibbonPoints; ++i) dd.line(rib[i], rib[i + 1], rgba(255, 120, 220));
        Vec3 base = hero_.interpolatedPos(alpha) + Vec3{0, 0.005f, 0};
        dd.circleXZ(base, hero_.body.radius, 20, rgba(120, 120, 120));
        dd.line(base, base + hero_.forward() * 0.6f, rgba(255, 255, 0));
        if (lastHitAge_ < 0.25f) {  // sector del último golpe
            const int n = 16;
            float half = lastHitCos_ <= -1 ? kPi : std::acos(clampf(lastHitCos_, -1, 1));
            float yaw = std::atan2(lastHitDir_.x, lastHitDir_.z);
            Vec3 o = lastHitOrigin_ + Vec3{0, 0.02f, 0};
            Vec3 prev = o;
            for (int i = 0; i <= n; ++i) {
                float a = yaw - half + 2.0f * half * float(i) / float(n);
                Vec3 p = o + Vec3{std::sin(a), 0, std::cos(a)} * lastHitRange_;
                dd.line(prev, p, rgba(255, 90, 60));
                prev = p;
            }
            dd.line(prev, o, rgba(255, 90, 60));
        }
        return;
    }
    if (blink) return;
    float flash = heroFlash_ > 0 ? 0.7f : 0;
    // Cuerpo continuo (malla con skinning) con contorno, y los detalles rígidos encima.
    const BuiltModel& body = model(ModelId::Hero);
    Mat4 skin[kMaxJoints];
    skinMatrices(body, j, skin);
    solid->skinned(body.mesh, skin, body.jointCount, flash, kOutline);
    for (const Shape& s : kHeroShapes) solid->ellipsoid(j[s.joint], s.center, s.radii, s.color, s.material, flash);
    for (const NeonLine& l : kNeonLines) solid->box(j[l.joint], l.center, l.half, l.color, Material::Glow, flash);
    solid->box(j[Chest] * translate({0, 0.12f, 0.128f}) * rotateZ(kPi * 0.25f), {}, {0.028f, 0.028f, 0.012f}, kNeonPink,
               Material::Glow, flash);  // núcleo del pecho
    // Mechones de pelo: se inclinan hacia donde se queda atrás el punto con muelle que
    // sigue a la cabeza (inercia al correr, saltar y girar).
    Vec3 lagW = hero_.hairLag();
    const float* hm = j[Head].m;
    Vec3 lag{hm[0] * lagW.x + hm[1] * lagW.y + hm[2] * lagW.z, hm[4] * lagW.x + hm[5] * lagW.y + hm[6] * lagW.z,
             hm[8] * lagW.x + hm[9] * lagW.y + hm[10] * lagW.z};
    Vec3 headCenter{0, 0.2f, -0.02f};
    for (Vec3 d : kSpikes) {
        Vec3 n = normalize(normalize(d) + lag * 5.0f);
        solid->ellipsoid(j[Head] * translate(headCenter + n * 0.13f) * basisY(n), {}, {0.06f, 0.14f, 0.055f}, kHair,
                         Material::Cloth, flash);
    }
    for (float x : {-0.07f, 0.0f, 0.07f}) {  // flequillo hacia un lado
        Vec3 n = normalize(Vec3{-0.6f + x * 2.0f, -0.5f, 1.0f} + lag * 3.0f);
        solid->ellipsoid(j[Head] * translate(Vec3{x, 0.27f, 0.1f}) * basisY(-n), {}, {0.045f, 0.09f, 0.035f}, kHair,
                         Material::Cloth, flash);
    }
    drawWeapon(*solid, j[WristR], ws, time_);
    // Cinta de la espalda: segmentos alargados entre los puntos de la cadena, que se
    // estrechan hacia la punta.
    Vec3 rib[Character::kRibbonPoints];
    hero_.ribbon(alpha, rib);
    for (int i = 0; i + 1 < Character::kRibbonPoints; ++i) {
        Vec3 a = rib[i], b = rib[i + 1], d = b - a;
        float len = length(d);
        if (len < 1e-4f) continue;
        float t = float(i) / float(Character::kRibbonPoints - 1);
        float wdt = lerpf(0.07f, 0.035f, t);
        uint32_t col = mixRgb(kNeon, kNeonPink, t);  // cinta de luz: del cian al magenta
        solid->ellipsoid(translate((a + b) * 0.5f) * basisY(d), {}, {wdt, len * 0.62f, 0.012f}, col, Material::Glow, flash);
    }
    if (guardFlash_ > 0)
        solid->light(translationOf(j[WristR]) + hero_.forward() * 0.3f,
                     (perfectGuard_ ? Vec3{0.5f, 0.9f, 1.0f} : Vec3{1.0f, 0.95f, 0.8f}) * (guardFlash_ * 12.0f), 3.0f);
}

void Game::drawEnemies(DebugDraw& dd, DebugDraw& glow, Renderer* solid, const DrawFlags& f, const ViewInfo& view,
                       float alpha) const {
    for (size_t i = 0; i < enemies_.size(); ++i) {
        const Enemy& e = enemies_[i];
        const EnemyDef& d = e.def();
        Mat4 joints[kMaxJoints];
        for (int k = 0; k < d.jointCount; ++k) joints[k] = lerpMat(e.prevJoints[k], e.joints[k], alpha);
        if (!solid) {
            uint32_t col = e.state == AiState::Windup  ? rgba(255, 200, 60)
                           : e.state == AiState::Attack ? rgba(255, 80, 60)
                           : e.hitFlash > 0             ? rgba(255, 255, 255)
                           : e.freeze > 0               ? rgba(120, 220, 255)
                                                        : rgba(200, 120, 255);
            drawSkeleton(dd, d.rig, d.jointCount, joints, f, col);
            dd.circleXZ(e.body.pos + Vec3{0, 0.01f, 0}, e.body.radius, 16, col);
            if (int(i) == lock_) dd.circleXZ(e.body.pos + Vec3{0, 0.02f, 0}, e.body.radius + 0.3f, 24, rgba(255, 220, 60));
            continue;
        }
        if (e.state == AiState::Spawning) {
            // Portal oscuro en el suelo del que sale.
            float k = 1.0f - smoothstep01((e.stateTime - 0.6f) / 0.3f);
            Vec3 base{e.body.pos.x, groundHeight(e.body.pos + Vec3{0, 0.5f, 0}) + 0.02f, e.body.pos.z};
            glowRing(glow, base, 0.2f, 0.9f * k, withAlpha(rgba(150, 70, 255), 0.5f * k));
            glowRing(glow, base, 0.75f * k, 0.95f * k, withAlpha(rgba(255, 150, 255), 0.8f * k));
        }
        bool dying = e.state == AiState::Dying;
        float flash = dying ? 0.4f + 0.6f * smoothstep01(e.stateTime / 0.3f) : e.hitFlash > 0 ? 0.75f : 0.0f;
        // Aviso del ataque: parpadeo cálido durante la preparación.
        if (e.state == AiState::Windup && std::fmod(e.stateTime, 0.16f) < 0.06f) flash = std::max(flash, 0.35f);
        // Interferencia: al recibir un golpe (y de vez en cuando) el cuerpo salta de sitio
        // un instante, como una señal que se corta.
        float glitchPhase = std::fmod(time_ * 0.7f + e.seed, 1.0f);
        if (!dying && (e.hitFlash > 0 || glitchPhase < 0.025f)) {
            float jx = std::sin(time_ * 97.0f + e.seed * 13.0f), jy = std::sin(time_ * 71.0f + e.seed * 7.0f);
            Mat4 jitter = translate(view.right * (0.07f * jx) + view.up * (0.03f * jy));
            for (int k = 0; k < d.jointCount; ++k) joints[k] = jitter * joints[k];
        }
        const BuiltModel& body = model(enemyModel(int(e.kind)));
        Mat4 skin[kMaxJoints];
        skinMatrices(body, joints, skin);
        // Congelado: algo de destello blanco azulado; el resto lo ponen las estrellas de escarcha.
        solid->skinned(body.mesh, skin, body.jointCount, std::max(flash, e.freeze > 0 ? 0.25f : 0.0f), rgba(20, 20, 30));
        for (int s = 0; s < d.shapeCount; ++s) {
            const EnemyShape& sh = d.shapes[s];
            if (!enemyShapeIsRigid(sh)) continue;
            solid->ellipsoid(joints[sh.joint], sh.center, sh.radii, sh.color, sh.glow ? Material::Glow : Material::Cloth, flash);
        }
        if (e.freeze > 0 && !dying)  // escarcha alrededor
            for (int k = 0; k < 2; ++k)
                glowStar(glow, view, e.center() + Vec3{std::sin(time_ * 2 + float(k) * 3), 0.3f * float(k), std::cos(time_ * 2 + float(k) * 3)} * 0.45f,
                         0.12f, time_ * 2.0f, withAlpha(rgba(170, 230, 255), 0.6f));
        if (e.kind == EnemyKind::Moth && !dying) solid->light(e.center(), Vec3{0.5f, 1.0f, 0.9f} * 0.6f, 2.5f);
        if (e.burn > 0 && !dying) solid->fireColumn(e.body.pos, d.radius + 0.1f, d.height * 1.3f, float(i) * 13.0f, std::min(1.0f, e.burn));
        // Retícula del objetivo fijado: rombos que giran alrededor.
        if (int(i) == lock_) {
            Vec3 c = e.center();
            float r = d.radius + 0.35f + 0.05f * std::sin(time_ * 6.0f);
            for (int k = 0; k < 4; ++k) {
                float a = time_ * 2.0f + float(k) * kPi * 0.5f;
                Vec3 p = c + (view.right * std::cos(a) + view.up * std::sin(a)) * r;
                Vec3 radial = normalize(p - c);
                Vec3 tangent = view.right * -std::sin(a) + view.up * std::cos(a);
                glowQuad(glow, p, radial * 0.09f, tangent * 0.05f, withAlpha(rgba(255, 200, 60), 0.9f));
            }
        }
    }
}

void Game::drawEffects(DebugDraw& dd, DebugDraw& glow, Renderer* solid, const ViewInfo& view) const {
    // Botín.
    for (const Pickup& p : pickups_) {
        float limit = p.kind == PickupKind::Item ? 60.0f : 25.0f;
        if (limit - p.age < 3.0f && std::fmod(p.age, 0.25f) < 0.1f) continue;  // parpadea antes de desaparecer
        float bob = p.resting ? 0.12f + 0.05f * std::sin(time_ * 3.0f + p.seed) : 0.05f;
        Vec3 c = p.pos + Vec3{0, bob, 0};
        uint32_t col = p.kind == PickupKind::Hp      ? rgba(110, 255, 130)
                       : p.kind == PickupKind::Mp    ? rgba(110, 160, 255)
                       : p.kind == PickupKind::Munny ? rgba(255, 210, 60)
                                                     : itemDef(p.item).color;
        if (!solid) {
            float s = 0.08f;
            dd.line(c - Vec3{s, 0, 0}, c + Vec3{s, 0, 0}, col);
            dd.line(c - Vec3{0, s, 0}, c + Vec3{0, s, 0}, col);
            dd.line(c - Vec3{0, 0, s}, c + Vec3{0, 0, s}, col);
            continue;
        }
        switch (p.kind) {
            case PickupKind::Hp:
            case PickupKind::Mp:
                solid->ellipsoid(Mat4::identity(), c, {0.08f, 0.08f, 0.08f}, col, Material::Glow);
                glowDot(glow, view, c, 0.22f, withAlpha(col, 0.35f));
                break;
            case PickupKind::Munny: {
                float r = p.value >= 10 ? 0.14f : p.value >= 5 ? 0.11f : 0.08f;
                Mat4 m = translate(c) * rotateY(time_ * 4.0f + p.seed);
                solid->ellipsoid(m, {}, {r, r, r * 0.3f}, col, Material::Metal);
                glowStar(glow, view, c, r * 1.4f, time_ * 1.5f + p.seed, withAlpha(rgba(255, 240, 160), 0.4f));
                break;
            }
            case PickupKind::Item: {
                Mat4 m = translate(c + Vec3{0, 0.1f, 0}) * rotateY(time_ * 1.5f + p.seed) * rotateX(kPi * 0.25f) *
                         rotateZ(kPi * 0.25f);
                solid->box(m, {}, {0.11f, 0.11f, 0.11f}, col, Material::Glow);
                glowDot(glow, view, c + Vec3{0, 0.1f, 0}, 0.35f, withAlpha(col, 0.3f));
                // Haz de luz vertical para verlo de lejos.
                glowQuad(glow, c + Vec3{0, 0.8f, 0}, view.right * 0.04f, Vec3{0, 0.8f, 0}, withAlpha(col, 0.25f));
                break;
            }
        }
    }

    // Proyectiles.
    for (const Projectile& p : projectiles_) {
        uint32_t col = p.kind == ProjectileKind::Fire  ? rgba(255, 150, 50)
                       : p.kind == ProjectileKind::Ice ? rgba(170, 235, 255)
                       : p.reflected                   ? rgba(255, 255, 200)
                                                       : rgba(255, 140, 230);
        if (!solid) {
            dd.point(p.pos, col);
            dd.line(p.pos, p.pos - p.vel * 0.05f, col);
            continue;
        }
        switch (p.kind) {
            case ProjectileKind::Fire:
                solid->fireBall(p.pos, 0.35f, p.seed, 1.4f);
                solid->light(p.pos, Vec3{1.0f, 0.5f, 0.15f} * 3.0f, 4.0f);
                glowDot(glow, view, p.pos, 0.3f, withAlpha(col, 0.5f));
                break;
            case ProjectileKind::Ice: {
                Vec3 d = normalize(p.vel);
                solid->ellipsoid(translate(p.pos) * basisY(d) * rotateY(time_ * 10.0f), {}, {0.06f, 0.2f, 0.06f}, col,
                                 Material::Glow);
                glowDot(glow, view, p.pos, 0.2f, withAlpha(col, 0.4f));
                break;
            }
            case ProjectileKind::EnemyOrb:
                solid->ellipsoid(Mat4::identity(), p.pos, {0.13f, 0.13f, 0.13f}, col, Material::Glow);
                solid->light(p.pos, Vec3{1.0f, 0.5f, 0.9f} * 1.5f, 3.0f);
                glowDot(glow, view, p.pos, 0.4f, withAlpha(col, 0.45f));
                glowStar(glow, view, p.pos, 0.35f, time_ * 4.0f + p.seed, withAlpha(col, 0.5f));
                break;
        }
    }

    // Estela del arma: tiras entre muestras consecutivas que se desvanecen.
    if (solid && trail_.size() > 1) {
        uint32_t col = inv_.weaponStats().trail;
        for (size_t i = 0; i + 1 < trail_.size(); ++i) {
            const TrailSample &a = trail_[i], &b = trail_[i + 1];
            float ka = 1.0f - a.age / kTrailLife, kb = 1.0f - b.age / kTrailLife;
            uint32_t ca = withAlpha(col, 0.5f * ka), cb = withAlpha(col, 0.5f * kb);
            glow.triangle(a.base, a.tip, b.tip, ca);
            glow.triangle(a.base, b.tip, b.base, cb);
        }
    }

    // Partículas: destellos y puntos suaves (en debug, puntos).
    for (const Particle& p : particles_) {
        float k = p.life / p.maxLife;
        if (!solid) {
            dd.point(p.pos, p.color);
            continue;
        }
        if (p.star)
            glowStar(glow, view, p.pos, p.size * (0.5f + k), p.life * 6.0f, withAlpha(p.color, k));
        else
            glowDot(glow, view, p.pos, p.size, withAlpha(p.color, 0.8f * k));
    }
    for (const Ring& r : rings_) {
        float u = r.age / r.life;
        float radius = r.radius * smoothstep01(u * 1.4f);
        if (!solid) {
            dd.circleXZ(r.pos, radius, 32, r.color);
            continue;
        }
        glowRing(glow, r.pos, std::max(0.0f, radius - 0.25f), radius, withAlpha(r.color, 0.8f * (1.0f - u)));
    }
    for (const Blast& b : blasts_) {
        float grow = smoothstep01(b.age / 0.15f), fade = 1.0f - b.age / 0.6f;
        if (!solid) {
            dd.circleXZ(b.pos, 2.2f * grow, 24, rgba(255, 170, 60));
            continue;
        }
        solid->fireBall(b.pos, b.radius * grow, b.seed, 1.4f * fade);
        solid->light(b.pos + Vec3{0, 0.5f, 0}, Vec3{1.0f, 0.5f, 0.15f} * (6.0f * fade), 8.0f);
    }
}

// --- HUD -------------------------------------------------------------------------------

void Game::drawHud(DebugDraw& hud, const ViewInfo& view, bool debug) const {
    const float w = float(view.width), h = float(view.height);

    // Retrato y barras de PV/PM abajo a la derecha (la longitud crece con el máximo).
    {
        float cx = w - 78, cy = h - 78;
        fillCircle(hud, cx, cy, 52, rgba(20, 24, 40, 230));
        fillCircle(hud, cx, cy, 46, hp_ / maxHp_ < 0.25f ? rgba(200, 60, 70) : rgba(60, 140, 230));
        fillCircle(hud, cx, cy + 4, 30, rgba(250, 214, 182));  // cara
        for (int i = 0; i < 5; ++i) {                            // pelo de punta
            float a = kPi * (1.1f + 0.2f * float(i));
            float bx = cx + std::cos(a) * 22, by = cy + std::sin(a) * 22 - 4;
            hud.triangle({bx - 12, by + 6, 0}, {bx + 12, by + 6, 0}, {cx + std::cos(a) * 46, cy + std::sin(a) * 46, 0},
                         rgba(128, 74, 42));
        }
        fillCircle(hud, cx, cy - 16, 26, rgba(128, 74, 42), 20);
        fillRect(hud, cx - 26, cy - 14, cx + 26, cy - 6, rgba(128, 74, 42));
        fillCircle(hud, cx - 10, cy + 4, 4, rgba(40, 70, 160), 10);
        fillCircle(hud, cx + 10, cy + 4, 4, rgba(40, 70, 160), 10);
        if (heroFlash_ > 0) fillCircle(hud, cx, cy, 46, withAlpha(rgba(255, 80, 80), heroFlash_ * 2.0f));

        float right = cx - 58;
        roundBar(hud, right, cy - 30, maxHp_ * 2.6f, 18, hp_ / maxHp_, hpGhost_ / maxHp_, rgba(90, 230, 90),
                 rgba(230, 60, 60));
        roundBar(hud, right, cy - 4, maxMp_ * 3.4f, 11, mp_ / maxMp_, mp_ / maxMp_, rgba(80, 150, 255), 0);
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%d/%d", int(std::ceil(hp_)), int(maxHp_));
        hudText(hud, right - textWidth(buf, 1.6f) - 6, cy - 50, 1.6f, buf, rgba(220, 255, 220));
        std::snprintf(buf, sizeof(buf), "PM %d", int(mp_));
        hudText(hud, right - maxMp_ * 3.4f - textWidth(buf, 1.4f) - 16, cy - 4, 1.4f, buf, rgba(200, 220, 255));
        // Dinero.
        fillCircle(hud, w - 170, cy + 50 - 2, 8, rgba(255, 210, 60), 14);
        fillCircle(hud, w - 170, cy + 50 - 2, 4, rgba(255, 240, 160), 10);
        std::snprintf(buf, sizeof(buf), "%d", inv_.munny);
        hudText(hud, w - 156, cy + 42, 2.2f, buf, rgba(255, 225, 110));
    }

    // Menú de comandos abajo a la izquierda: magias (1-3) y atajos de objetos (4-6).
    {
        const float x = 24, rowH = 30, width = 270;
        float y0 = h - 24 - rowH * float(kCommandCount) - 34;
        fillRect(hud, x, y0, x + 150, y0 + 26, rgba(40, 70, 160, 230));
        hudText(hud, x + 10, y0 + 7, 2.0f, "COMANDOS", rgba(255, 255, 255));
        for (int i = 0; i < kCommandCount; ++i) {
            float y = y0 + 30 + float(i) * rowH;
            bool sel = i == commandSel_;
            char label[64];
            bool usable;
            if (i < 3) {
                const SpellDef& s = spellDef(Spell(i));
                std::snprintf(label, sizeof(label), "%d %-6s %2d PM", i + 1, s.name, int(s.cost));
                usable = mp_ >= s.cost;
            } else {
                ItemId id = inv_.quick[i - 3];
                std::snprintf(label, sizeof(label), "%d %-13s X%d", i + 1, itemName(id), inv_.count(id));
                usable = inv_.count(id) > 0;
            }
            float x0 = x + (sel ? 14.0f : 0.0f);
            fillRect(hud, x0, y, x0 + width, y + rowH - 4, sel ? rgba(250, 250, 255, 235) : rgba(20, 26, 50, 210));
            if (sel) {
                fillRect(hud, x0, y, x0 + 6, y + rowH - 4, rgba(255, 200, 60));
                hud.triangle({x0 - 12, y + 4, 0}, {x0 - 12, y + rowH - 8, 0}, {x0 - 3, y + (rowH - 4) * 0.5f, 0},
                             rgba(255, 200, 60));
            }
            uint32_t tc = !usable ? rgba(120, 120, 135) : sel ? rgba(30, 40, 90) : rgba(235, 240, 255);
            if (i < 3 && usable) tc = sel ? scaleRgb(spellDef(Spell(i)).color, 0.6f) : spellDef(Spell(i)).color;
            drawText(hud, x0 + 14, y + 7, 2.0f, label, tc);
        }
        hudText(hud, x, h - 26, 1.3f, "RUEDA: ELEGIR   E: USAR   CLIC: ATACAR   I: INVENTARIO", rgba(200, 210, 240));
    }

    // Objetivo arriba a la derecha: nombre y vida del fijado.
    if (lock_ >= 0) {
        const Enemy& e = enemies_[size_t(lock_)];
        const EnemyDef& d = e.def();
        float right = w - 30, len = 220;
        hudText(hud, right - textWidth(d.name, 2.2f), 24, 2.2f, d.name, rgba(255, 230, 150));
        roundBar(hud, right, 48, len, 12, std::max(0.0f, e.health) / d.maxHealth, std::max(0.0f, e.health) / d.maxHealth,
                 rgba(255, 190, 60), 0);
    }

    // Avisos de objetos obtenidos (a la derecha, se desvanecen).
    for (size_t i = 0; i < notices_.size(); ++i) {
        const Notice& n = notices_[i];
        float a = clampf(std::min(n.age / 0.15f, (4.0f - n.age) / 0.6f), 0, 1);
        float slide = (1.0f - smoothstep01(n.age / 0.25f)) * 60.0f;
        float tw = textWidth(n.text.c_str(), 2.0f);
        float y = 90 + float(i) * 34, x1 = w - 30 + slide, x0 = x1 - tw - 30;
        fillRect(hud, x0, y, x1, y + 28, withAlpha(rgba(20, 26, 50), 0.85f * a));
        fillRect(hud, x0, y, x0 + 5, y + 28, withAlpha(n.color, a));
        drawText(hud, x0 + 16, y + 8, 2.0f, n.text.c_str(), withAlpha(n.color, a));
    }

    // Oleada y derrotados arriba a la izquierda; cartel grande al empezar cada oleada.
    {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "OLEADA %d   DERROTADOS %d", wave_, kills_);
        hudText(hud, 24, 24, 2.0f, buf, rgba(255, 255, 255));
        if (waveBanner_ > 0) {
            float a = clampf(std::min((2.5f - waveBanner_) / 0.3f, waveBanner_ / 0.5f), 0, 1);
            std::snprintf(buf, sizeof(buf), "OLEADA %d", wave_);
            float tw = textWidth(buf, 7);
            fillRect(hud, 0, h * 0.22f - 20, w, h * 0.22f + 62, withAlpha(rgba(30, 50, 120), 0.55f * a));
            hudText(hud, (w - tw) * 0.5f, h * 0.22f, 7, buf, withAlpha(rgba(255, 230, 120), a));
        } else if (enemies_.empty() && wave_ > 0) {
            const char* msg = "OLEADA SUPERADA";
            hudText(hud, (w - textWidth(msg, 4)) * 0.5f, h * 0.22f, 4, msg, rgba(160, 255, 180));
        }
    }

    int chest = nearbyChest();
    if (chest >= 0 && hero_.downTimer < 0) {
        const char* msg = "F: ABRIR COFRE";
        float tw = textWidth(msg, 2.6f);
        fillRect(hud, (w - tw) * 0.5f - 16, h * 0.68f - 10, (w + tw) * 0.5f + 16, h * 0.68f + 26, rgba(20, 26, 50, 200));
        hudText(hud, (w - tw) * 0.5f, h * 0.68f, 2.6f, msg, rgba(255, 230, 120));
    }
    if (hero_.downTimer >= 0) {
        const char* msg = "HAS CAIDO...";
        hudText(hud, (w - textWidth(msg, 6)) * 0.5f, h * 0.4f, 6, msg, rgba(255, 120, 120));
    }

    // Modo debug: estado de la IA y vida encima de cada enemigo, y recuentos.
    if (debug) {
        for (const Enemy& e : enemies_) {
            float sx, sy;
            if (!project(view, e.center() + Vec3{0, e.def().height * 0.5f + 0.3f, 0}, sx, sy)) continue;
            char buf[64];
            std::snprintf(buf, sizeof(buf), "%s %.0f", aiStateName(e.state), double(std::max(0.0f, e.health)));
            drawText(hud, sx - textWidth(buf, 1.4f) * 0.5f, sy, 1.4f, buf, rgba(255, 230, 120));
        }
        char buf[128];
        std::snprintf(buf, sizeof(buf), "DEBUG  ENEMIGOS %zu  BOTIN %zu  PARTICULAS %zu  PROYECTILES %zu",
                      enemies_.size(), pickups_.size(), particles_.size(), projectiles_.size());
        drawText(hud, 24, 50, 1.5f, buf, rgba(120, 255, 160));
        drawText(hud, 24, 66, 1.5f, "F1 HUESOS  F2 ARTICULACIONES  F3 CAJAS  F4 REJILLA", rgba(120, 200, 160));
    }
}
