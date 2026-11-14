#include "game.h"

#include "renderer.h"
#include "models.h"
#include "text.h"

#include <cstdio>

namespace {

uint32_t withAlpha(uint32_t c, float a) { return (c & 0x00FFFFFFu) | uint32_t(clampf(a, 0, 1) * 255.0f + 0.5f) << 24; }
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

// Arma prisma: empuñadura blanca con anillo de luz y una forma de luz sólida que cambia
// con el golpe: hoja (tajos), látigo (giros) y martillo (remate). La hoja sale de la
// muñeca a lo largo de +Z local.
void drawWeapon(Renderer& r, const Mat4& w, const WeaponStats& ws, Move m, float u, float time) {
    float len = ws.length;
    // Cuánto de látigo o de martillo: entra y sale suave al principio y al final del golpe.
    float morph = smoothstep01(u / 0.15f) * smoothstep01((1.0f - u) / 0.15f);
    float whip = (m == Move::Slash3 || m == Move::AirFinisher) ? morph : 0.0f;
    float hammer = m == Move::Finisher ? morph : 0.0f;
    const uint32_t grip = rgba(235, 238, 248);
    if (ws.style == WeaponStyle::Fists) {  // guantelete: funda clara y nudillos de luz
        r.ellipsoid(w, {0, -0.06f, 0.008f}, {0.062f, 0.088f, 0.072f}, ws.blade, Material::Metal);
        r.ellipsoid(w, {0, -0.115f, 0.03f}, {0.055f, 0.022f, 0.05f}, ws.trail, Material::Glow);
        r.ellipsoid(w, {0, 0.01f, 0}, {0.06f, 0.025f, 0.06f}, ws.trail, Material::Glow);  // puño
        return;
    }
    r.box(w, kGripLocal, {0.02f, 0.02f, 0.075f}, grip, Material::Metal);
    r.ellipsoid(w, kGripLocal + Vec3{0, 0, 0.09f}, {0.07f, 0.07f, 0.025f}, grip, Material::Metal);
    r.ellipsoid(w, kGripLocal + Vec3{0, 0, 0.1f}, {0.055f, 0.055f, 0.02f}, ws.trail, Material::Glow);
    r.ellipsoid(w, kGripLocal - Vec3{0, 0, 0.08f}, {0.03f, 0.03f, 0.03f}, ws.trail, Material::Glow);  // pomo

    if (whip > 0.01f) {
        // Látigo: cadena de cuentas de luz, más larga, que se curva hacia atrás.
        const int n = 14;
        float total = len * (1.0f + 1.3f * whip);
        for (int i = 0; i < n; ++i) {
            float t = float(i + 1) / float(n);
            Vec3 p = kBladeBase + Vec3{-0.45f * whip * t * t, 0.05f * std::sin(time * 20.0f + t * 6.0f) * whip, total * t};
            float rad = lerpf(0.035f, 0.022f, t);
            r.ellipsoid(w, p, {rad, rad, rad * 1.6f}, ws.trail, Material::Glow);
        }
        return;
    }
    Vec3 base = kBladeBase, tip = kBladeBase + Vec3{0, 0, len};
    Vec3 mid = (base + tip) * 0.5f;
    if (ws.style == WeaponStyle::Lance) {  // lanza: asta larga (también hacia atrás) y punta de luz
        r.box(w, mid - Vec3{0, 0, 0.25f}, {0.017f, 0.017f, len * 0.5f + 0.25f}, ws.blade, Material::Metal);
        r.box(w, mid, {0.006f, 0.02f, len * 0.45f}, ws.trail, Material::Glow);
        r.ellipsoid(w, tip + Vec3{0, 0, 0.08f}, {0.05f, 0.015f, 0.17f}, ws.trail, Material::Glow);
        r.ellipsoid(w, tip - Vec3{0, 0, 0.04f}, {0.07f, 0.02f, 0.03f}, ws.blade, Material::Metal);
        return;
    }
    // Hoja: núcleo luminoso del color del prisma dentro de una funda clara.
    r.box(w, mid, {0.04f, 0.011f, len * 0.5f}, ws.blade, Material::Metal);
    r.box(w, mid, {0.014f, 0.015f, len * 0.48f}, ws.trail, Material::Glow);
    r.ellipsoid(w, tip, {0.045f, 0.018f, 0.09f}, ws.trail, Material::Glow);
    if (hammer > 0.01f) {  // martillo: una cabeza de luz que crece en la punta
        Vec3 head = tip + Vec3{0, 0, 0.05f};
        r.box(w, head, Vec3{0.16f, 0.11f, 0.11f} * hammer, ws.blade, Material::Metal);
        r.box(w, head, Vec3{0.17f, 0.06f, 0.06f} * hammer, ws.trail, Material::Glow);
    }
    if (ws.style == WeaponStyle::Staff) {  // el prisma estelar lleva una estrella en la punta
        Mat4 star = w * translate(tip + Vec3{0, 0, 0.06f}) * rotateZ(time * 2.0f);
        for (int i = 0; i < 5; ++i) {
            float a = 2.0f * kPi * float(i) / 5.0f;
            Vec3 d{std::cos(a), std::sin(a), 0};
            r.ellipsoid(star * translate(d * 0.06f) * basisY(d), {}, {0.02f, 0.05f, 0.014f}, rgba(255, 230, 120), Material::Glow);
        }
    }
}

// --- HUD: primitivas 2D en píxeles (origen arriba a la izquierda) ---

const uint32_t kUiPanel = rgba(12, 16, 42, 205), kUiCyan = rgba(40, 220, 255), kUiPink = rgba(255, 70, 190);
const uint32_t kUiText = rgba(240, 245, 255), kUiDim = rgba(150, 165, 210), kUiAmber = rgba(255, 196, 70);
const uint32_t kUiShadow = rgba(8, 10, 30, 200);

void fillRect(DebugDraw& d, float x0, float y0, float x1, float y1, uint32_t c) {
    d.quad({x0, y0, 0}, {x1, y0, 0}, {x1, y1, 0}, {x0, y1, 0}, c);
}
// Paralelogramo: la parte de arriba desplazada `skew` píxeles a la derecha.
void skewRect(DebugDraw& d, float x0, float y0, float x1, float y1, float skew, uint32_t c) {
    d.quad({x0 + skew, y0, 0}, {x1 + skew, y0, 0}, {x1, y1, 0}, {x0, y1, 0}, c);
}
void fillCircle(DebugDraw& d, float cx, float cy, float r, uint32_t c, int n = 28, float rot = 0) {
    for (int i = 0; i < n; ++i) {
        float a0 = rot + 2.0f * kPi * float(i) / float(n), a1 = rot + 2.0f * kPi * float(i + 1) / float(n);
        d.triangle({cx, cy, 0}, {cx + std::cos(a0) * r, cy + std::sin(a0) * r, 0},
                   {cx + std::cos(a1) * r, cy + std::sin(a1) * r, 0}, c);
    }
}
void diamond(DebugDraw& d, float cx, float cy, float r, uint32_t c) {
    d.quad({cx, cy - r, 0}, {cx + r, cy, 0}, {cx, cy + r, 0}, {cx - r, cy, 0}, c);
}
// Barra inclinada y segmentada (cada `segment` unidades una muesca), con lo recién
// perdido en otro color.
void segBar(DebugDraw& d, float xRight, float y, float length, float thick, float frac, float ghost, uint32_t fill,
            uint32_t ghostCol, int segments) {
    const float skew = thick * 0.6f, x0 = xRight - length;
    skewRect(d, x0 - 4, y - 3, xRight + 4, y + thick + 3, skew, kUiPanel);
    auto seg = [&](float a, float b, uint32_t c) {
        if (b <= a) return;
        skewRect(d, x0 + length * a, y, x0 + length * b, y + thick, skew, c);
    };
    seg(0, 1, rgba(40, 36, 70, 230));
    seg(frac, ghost, ghostCol);
    seg(0, frac, fill);
    if (frac > 0) skewRect(d, x0, y, x0 + length * frac, y + thick * 0.35f, skew * 0.35f, withAlpha(rgba(255, 255, 255), 0.3f));
    for (int i = 1; i < segments; ++i) {  // muescas
        float x = x0 + length * float(i) / float(segments);
        d.quad({x + skew - 1, y, 0}, {x + skew + 1, y, 0}, {x + 1, y + thick, 0}, {x - 1, y + thick, 0}, kUiPanel);
    }
}

void uiText(UiLayer& ui, float x, float y, float size, const char* s, uint32_t c, FontFace face = FontFace::Body,
            Align align = Align::Left, float glow = 0) {
    TextStyle st;
    st.face = face;
    st.align = align;
    st.glow = glow;
    st.outline = rgba(8, 10, 30, 190);
    drawText(ui, x, y, size, s, c, st);
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
    drawStory(dd, glow, solid, view);
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
        for (const Gate& g : gates_)
            if (g.closed) dd.box(id, g.box.center(), g.box.halfExtents(), rgba(255, 70, 190));
        for (const ColorNode& n : nodes_) dd.box(id, n.pos + Vec3{0, 0.8f, 0}, {0.3f, 0.8f, 0.3f}, n.lit ? n.color : rgba(140, 140, 150));
        for (const Lantern& l : lanterns_) dd.line(l.pos, l.pos + Vec3{0, 2.4f, 0}, l.lit ? rgba(255, 200, 120) : rgba(140, 140, 150));
        for (const Encounter& e : encounters_)
            if (!e.cleared) dd.box(id, e.trigger.center(), e.trigger.halfExtents(), e.started ? rgba(255, 90, 60) : rgba(90, 90, 120));
        for (size_t b = 0; b < beamPaths_.size(); ++b)
            for (size_t i = 0; i + 1 < beamPaths_[b].size(); ++i) dd.line(beamPaths_[b][i], beamPaths_[b][i + 1], beamColors_[b][i]);
        for (const Mirror& m : mirrors_) {
            float a = float(m.angle) * kPi * 0.25f;
            Vec3 c = m.pos + Vec3{0, 1.4f, 0}, t{-std::sin(a), 0, std::cos(a)};
            dd.line(c - t * 0.4f, c + t * 0.4f, rgba(255, 255, 255));
        }
        return;
    }
    // Todo el escenario se ve con el croma actual; las salpicaduras de color lo devuelven.
    solid->setWorldColor(cromaShown_);
    solid->setScenery(true);
    for (const ColorSplash& sp : splashes_) solid->paint(sp.pos, sp.radius * smoothstep01(sp.age / 0.3f), sp.color);
    // Suelo del escenario o, sobre el vacío, un mar de nubes muy abajo.
    if (world_.ground)
        solid->box(id, {0, -0.5f, 0}, {200, 0.5f, 200}, floorColor_, floorMaterial_);
    else
        solid->box(id, {0, -40.5f, 0}, {400, 0.5f, 400}, rgba(200, 205, 230), Material::Water);
    // Barreras de energía cerradas: marco luminoso (y la cortina translúcida, en efectos).
    for (const Gate& g : gates_) {
        if (!g.closed) continue;
        Vec3 c = g.box.center(), hh = g.box.halfExtents();
        bool alongX = hh.x > hh.z;
        Vec3 side = alongX ? Vec3{hh.x, 0, 0} : Vec3{0, 0, hh.z};
        for (float sgn : {-1.0f, 1.0f}) solid->box(id, c + side * sgn, {0.08f, hh.y, 0.08f}, g.color, Material::Glow);
        solid->box(id, c + Vec3{0, hh.y, 0}, alongX ? Vec3{hh.x, 0.05f, 0.08f} : Vec3{0.08f, 0.05f, hh.z}, g.color, Material::Glow);
        solid->box(id, c - Vec3{0, hh.y, 0}, alongX ? Vec3{hh.x, 0.05f, 0.08f} : Vec3{0.08f, 0.05f, hh.z}, g.color, Material::Glow);
    }
    // Nodos cromáticos: pedestal y cristal; encendido, gira, brilla y devuelve el color a su
    // alrededor (la zona crece durante unos segundos).
    for (const ColorNode& n : nodes_) {
        solid->box(id, n.pos + Vec3{0, 0.5f, 0}, {0.45f, 0.5f, 0.45f}, rgba(226, 230, 240), Material::Stone);
        solid->box(id, n.pos + Vec3{0, 1.02f, 0}, {0.5f, 0.03f, 0.5f}, n.color, Material::Glow);
        float bob = n.lit ? 0.12f * std::sin(time_ * 2.0f) : 0.0f;
        Mat4 m = translate(n.pos + Vec3{0, 1.75f + bob, 0}) * rotateY(n.lit ? time_ * 1.2f : 0.3f);
        solid->box(m * rotateX(kPi * 0.25f) * rotateZ(kPi * 0.25f), {}, {0.3f, 0.3f, 0.3f}, n.lit ? n.color : rgba(120, 122, 132),
                   n.lit ? Material::Glow : Material::Metal);
        if (n.lit) {
            float grow = smoothstep01(n.litTime / 3.0f);
            solid->colorZone(n.pos, n.radius * grow);
            solid->light(n.pos + Vec3{0, 1.8f, 0}, Vec3{1.0f, 0.9f, 0.8f} * 1.5f, 6.0f);
        }
    }
    // Rompecabezas de luz: emisores, espejos (panel que gira sobre un pedestal) y
    // receptores. El haz se dibuja aparte (efectos).
    for (const LightEmitter& em : emitters_) {
        Mat4 m = translate(em.pos) * rotateY(time_);
        solid->box(m * rotateX(kPi * 0.25f) * rotateZ(kPi * 0.25f), {}, {0.12f, 0.12f, 0.12f}, em.color, Material::Glow);
        solid->light(em.pos, Vec3{1.0f, 0.95f, 0.85f} * 1.2f, 3.0f);
    }
    for (const Mirror& mr : mirrors_) {
        solid->box(id, mr.pos + Vec3{0, 0.5f, 0}, {0.3f, 0.5f, 0.3f}, rgba(226, 230, 240), Material::Stone);
        float a = float(mr.angle) * kPi * 0.25f;
        // El panel es perpendicular a su normal (a, en el plano XZ).
        Mat4 panel = translate(mr.pos + Vec3{0, 1.4f, 0}) * rotateY(kPi * 0.5f - a);
        solid->box(panel, {}, {0.42f, 0.38f, 0.03f}, rgba(150, 190, 220), Material::Metal);
        uint32_t edge = mr.filter ? mr.filter : rgba(255, 245, 220);
        solid->box(panel, {0, 0.4f, 0}, {0.44f, 0.025f, 0.04f}, edge, Material::Glow);
        solid->box(panel, {0, -0.4f, 0}, {0.44f, 0.025f, 0.04f}, edge, Material::Glow);
        if (mr.filter) solid->box(panel, {}, {0.3f, 0.26f, 0.035f}, mr.filter, Material::Glow);
    }
    for (const Receptor& rc : receptors_) {
        uint32_t col = rc.needColor ? rc.needColor : rgba(255, 245, 220);
        Mat4 m = translate(rc.pos) * rotateY(rc.lit ? time_ * 2.0f : 0.0f);
        solid->ellipsoid(m, {}, {0.2f, 0.2f, 0.2f}, rc.lit ? col : rgba(110, 112, 125), rc.lit ? Material::Glow : Material::Metal);
        for (int k = 0; k < 8; ++k)  // anillo del color que pide
            solid->box(m * rotateZ(float(k) * kPi * 0.25f), {0, 0.32f, 0}, {0.03f, 0.06f, 0.03f}, col,
                       rc.lit || std::fmod(time_, 1.5f) < 0.75f ? Material::Glow : Material::Metal);
        if (rc.lit) solid->light(rc.pos, Vec3{1.0f, 0.9f, 0.8f} * 1.5f, 4.0f);
    }
    // Faroles del puente: poste y lámpara (apagada, gris; encendida, cálida).
    for (const Lantern& l : lanterns_) {
        solid->box(id, l.pos + Vec3{0, 1.1f, 0}, {0.07f, 1.1f, 0.07f}, rgba(226, 230, 240), Material::Metal);
        solid->box(id, l.pos + Vec3{0.2f, 2.15f, 0}, {0.25f, 0.03f, 0.03f}, rgba(226, 230, 240), Material::Metal);
        uint32_t lamp = l.lit ? rgba(255, 190, 110) : rgba(120, 122, 132);
        solid->ellipsoid(id, l.pos + Vec3{0.42f, 1.95f, 0}, {0.14f, 0.18f, 0.14f}, lamp, l.lit ? Material::Glow : Material::Metal);
        if (l.lit) {
            solid->light(l.pos + Vec3{0.42f, 1.95f, 0}, Vec3{1.0f, 0.7f, 0.4f} * 1.6f, 5.0f);
            solid->colorZone(l.pos, 6.0f * smoothstep01(l.litTime / 2.0f));
        }
    }
    // Objetos con los que se puede interactuar: un destello que invita a acercarse.
    for (const Interactable& it : interacts_)
        if (!it.used) solid->light(it.pos + Vec3{0, 1.5f, 0}, Vec3{1.0f, 0.8f, 0.5f} * (0.8f + 0.4f * std::sin(time_ * 3.0f)), 3.0f);
    // Prisma de la fuente (solo en la plaza): flota, gira y proyecta luz cambiante.
    if (level_ == LevelId::Plaza) {
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
    // Bombillas de las farolas (apagadas, grises) y la luz de las cercanas: cada luz cuesta
    // en todos los píxeles de la escena.
    for (const Lamp& l : lamps_) {
        if (l.bulb)
            solid->ellipsoid(translate(l.pos), {}, {0.2f, 0.2f, 0.2f}, l.on ? l.color : rgba(70, 72, 80),
                             l.on ? Material::Glow : Material::Metal);
        if (l.on && l.bulb && length(flat(l.pos - hero_.body.pos)) < 14.0f) {
            float r = float((l.color >> 0) & 0xFF) / 255.0f, g = float((l.color >> 8) & 0xFF) / 255.0f,
                  b = float((l.color >> 16) & 0xFF) / 255.0f;
            solid->light(l.pos, Vec3{0.4f + r * 0.6f, 0.4f + g * 0.6f, 0.4f + b * 0.6f} * 1.3f, 5.0f);
        }
    }

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
    solid->setScenery(false);
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
    if (inLight_) {  // dentro de la fuente: la bombilla late con más fuerza
        float pulse = 1.0f + 0.15f * std::sin(time_ * 6.0f);
        solid->ellipsoid(translate(hostPos_), {}, Vec3{0.32f, 0.32f, 0.32f} * pulse, rgba(255, 248, 225), Material::Glow);
        solid->light(hostPos_, Vec3{1.0f, 0.95f, 0.8f} * 3.5f, 8.0f);
        return;
    }
    if (travel_) {  // hecho luz: una estela alargada en lugar del cuerpo
        Vec3 c = hero_.interpolatedPos(alpha) + Vec3{0, 1.0f, 0};
        Vec3 d = normalize(travelTo_ - travelFrom_);
        if (length(d) < 0.5f) d = {0, 1, 0};
        solid->ellipsoid(translate(c) * basisY(d), {}, {0.16f, 1.3f, 0.16f}, rgba(255, 245, 215), Material::Glow);
        solid->ellipsoid(translate(c - d * 0.3f) * basisY(d), {}, {0.25f, 0.9f, 0.25f}, rgba(120, 220, 255), Material::Glow);
        solid->light(c, Vec3{1.0f, 0.95f, 0.85f} * 3.0f, 6.0f);
        return;
    }
    float flash = heroFlash_ > 0 ? 0.7f : 0;
    // Cuerpo continuo (malla con skinning) con contorno, y los detalles rígidos encima.
    const BuiltModel& body = model(ModelId::Hero);
    Mat4 skin[kMaxJoints];
    skinMatrices(body, j, skin);
    solid->skinned(body.mesh, skin, body.jointCount, flash, form_ != Form::None ? formColor(form_) : kOutline);
    // Formas prismáticas: piezas de luz propias de cada una.
    if (form_ == Form::Aurora) {  // halo que gira sobre la cabeza
        Mat4 halo = j[Head] * translate({0, 0.42f, -0.02f}) * rotateY(time_ * 2.5f);
        for (int k = 0; k < 10; ++k)
            solid->ellipsoid(halo * rotateY(float(k) * kPi * 0.2f), {0.2f, 0, 0}, {0.03f, 0.03f, 0.05f}, formColor(form_), Material::Glow);
    } else if (form_ == Form::Colossus) {  // cristales en los hombros y nudillos ardientes
        for (Joint sh : {ShoulderL, ShoulderR}) {
            float sx = sh == ShoulderL ? 1.0f : -1.0f;
            for (int k = 0; k < 3; ++k)
                solid->box(j[sh] * translate({sx * (0.06f + 0.03f * float(k)), 0.06f + 0.04f * float(k), -0.02f}) *
                               rotateZ(sx * (0.4f + 0.3f * float(k))),
                           {}, {0.025f, 0.09f, 0.025f}, formColor(form_), Material::Glow);
        }
        for (Joint wr : {WristL, WristR}) solid->ellipsoid(j[wr], {0, -0.1f, 0.02f}, {0.07f, 0.05f, 0.07f}, formColor(form_), Material::Glow);
    } else if (form_ == Form::Echo) {  // alas de luz que aletean
        float flap = 0.35f + 0.3f * std::sin(time_ * 6.0f);
        for (float sx : {-1.0f, 1.0f}) {
            Mat4 wing = j[Chest] * translate({sx * 0.1f, 0.16f, -0.16f}) * rotateY(sx * -0.5f) * rotateZ(sx * flap);
            solid->ellipsoid(wing, {sx * 0.42f, 0.1f, 0}, {0.42f, 0.22f, 0.015f}, formColor(form_), Material::Glow);
            solid->ellipsoid(wing, {sx * 0.3f, -0.15f, 0}, {0.28f, 0.14f, 0.015f}, rgba(255, 255, 255), Material::Glow);
        }
    }
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
    drawWeapon(*solid, j[WristR], ws, hero_.move(), hero_.moveU(), time_);
    if (ws.style == WeaponStyle::Fists) drawWeapon(*solid, j[WristL], ws, hero_.move(), hero_.moveU(), time_);
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
        if (e.kind == EnemyKind::Watcher && !dying) solid->light(e.center(), Vec3{1.0f, 0.9f, 0.7f} * 0.7f, 3.0f);
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
    // Cortinas de las barreras de energía: bandas que suben.
    if (solid)
        for (const Gate& g : gates_) {
            if (!g.closed) continue;
            Vec3 c = g.box.center(), hh = g.box.halfExtents();
            Vec3 side = hh.x > hh.z ? Vec3{hh.x, 0, 0} : Vec3{0, 0, hh.z};
            const int bands = 8;
            for (int i = 0; i < bands; ++i) {
                float y0 = -hh.y + 2.0f * hh.y * float(i) / bands, y1 = y0 + 2.0f * hh.y / bands;
                float a = 0.12f + 0.1f * std::sin(time_ * 4.0f - float(i) * 0.8f);
                glow.quad(c + Vec3{0, y0, 0} - side, c + Vec3{0, y0, 0} + side, c + Vec3{0, y1, 0} + side, c + Vec3{0, y1, 0} - side,
                          withAlpha(g.color, a));
            }
        }
    // Haces de luz de los rompecabezas: cinta orientada a la cámara + núcleo para el bloom.
    if (solid)
        for (size_t b = 0; b < beamPaths_.size(); ++b)
            for (size_t i = 0; i + 1 < beamPaths_[b].size(); ++i) {
                Vec3 p0 = beamPaths_[b][i], p1 = beamPaths_[b][i + 1];
                Vec3 dir = p1 - p0;
                float len = length(dir);
                if (len < 1e-3f) continue;
                Vec3 side = normalize(cross(dir, (p0 + p1) * 0.5f - view.eye)) * (0.06f + 0.02f * std::sin(time_ * 8.0f));
                uint32_t col = beamColors_[b][i];
                glow.quad(p0 - side, p1 - side, p1 + side, p0 + side, withAlpha(col, 0.8f));
                glow.quad(p0 - side * 3.0f, p1 - side * 3.0f, p1 + side * 3.0f, p0 + side * 3.0f, withAlpha(col, 0.25f));
            }
    for (const Interactable& it : interacts_)
        if (!it.used && solid) glowStar(glow, view, it.pos + Vec3{0, 1.6f + 0.1f * std::sin(time_ * 2.0f), 0}, 0.25f, time_, withAlpha(rgba(255, 220, 150), 0.8f));
    // Botín.
    for (const Pickup& p : pickups_) {
        float limit = p.kind == PickupKind::Item ? 60.0f : 25.0f;
        if (limit - p.age < 3.0f && std::fmod(p.age, 0.25f) < 0.1f) continue;  // parpadea antes de desaparecer
        float bob = p.resting ? 0.12f + 0.05f * std::sin(time_ * 3.0f + p.seed) : 0.05f;
        Vec3 c = p.pos + Vec3{0, bob, 0};
        uint32_t col = p.kind == PickupKind::Hp      ? rgba(110, 255, 130)
                       : p.kind == PickupKind::Mp    ? rgba(255, 226, 160)
                       : p.kind == PickupKind::Shadow ? rgba(150, 80, 255)
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
            case PickupKind::Shadow:
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
        uint32_t col = p.kind == ProjectileKind::Fire  ? rgba(255, 235, 160)
                       : p.kind == ProjectileKind::Ice ? rgba(170, 235, 255)
                       : p.reflected                   ? rgba(255, 255, 200)
                                                       : rgba(255, 140, 230);
        if (!solid) {
            dd.point(p.pos, col);
            dd.line(p.pos, p.pos - p.vel * 0.05f, col);
            continue;
        }
        switch (p.kind) {
            case ProjectileKind::Wave: {  // onda del corte cargado: arco de luz que avanza
                Vec3 d = normalize(flat(p.vel)), sd{-d.z, 0, d.x};
                float fade = clampf(p.life / 0.3f, 0, 1);
                uint32_t wc = inv_.weaponStats().trail;
                for (int k = -4; k < 4; ++k) {
                    float a0 = float(k) * 0.2f, a1 = float(k + 1) * 0.2f;
                    Vec3 q0 = p.pos + sd * std::sin(a0) * 1.4f - d * (1.0f - std::cos(a0)) * 1.4f;
                    Vec3 q1 = p.pos + sd * std::sin(a1) * 1.4f - d * (1.0f - std::cos(a1)) * 1.4f;
                    glow.quad(q0 - Vec3{0, 0.5f, 0}, q1 - Vec3{0, 0.5f, 0}, q1 + Vec3{0, 0.5f, 0}, q0 + Vec3{0, 0.5f, 0},
                              withAlpha(wc, 0.5f * fade));
                }
                solid->box(translate(p.pos) * rotateY(std::atan2(d.x, d.z)), {}, Vec3{1.2f, 0.4f, 0.04f} * fade, wc, Material::Glow);
                break;
            }
            case ProjectileKind::Fire: {  // lanza de luz
                Vec3 d = normalize(p.vel);
                solid->ellipsoid(translate(p.pos) * basisY(d), {}, {0.07f, 0.5f, 0.07f}, rgba(255, 245, 210), Material::Glow);
                solid->light(p.pos, Vec3{1.0f, 0.9f, 0.6f} * 2.5f, 4.0f);
                glowDot(glow, view, p.pos, 0.3f, withAlpha(rgba(255, 230, 150), 0.5f));
                break;
            }
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

// --- Historia: Brizna, ecos de memoria e interfaz de diálogo ----------------------------

void Game::drawStory(DebugDraw& dd, DebugDraw& glow, Renderer* solid, const ViewInfo& view) const {
    // Brizna: una chispa de luz cálida con dos destellos que la orbitan; late al hablar.
    const StoryLine* l = dialogue_.line();
    bool speaking = l && l->speaker == "brizna" && !dialogue_.lineComplete();
    float pulse = 1.0f + (speaking ? 0.35f * std::sin(time_ * 30.0f) : 0.12f * std::sin(time_ * 3.0f));
    uint32_t core = mixRgb(rgba(255, 200, 110), rgba(255, 150, 200), 0.5f + 0.5f * std::sin(time_ * 0.7f));
    if (!solid) {
        dd.point(briznaPos_, core);
        dd.circleXZ(briznaPos_, 0.12f, 12, core);
    } else {
        solid->ellipsoid(Mat4::identity(), briznaPos_, Vec3{0.055f, 0.055f, 0.055f} * pulse, core, Material::Glow);
        solid->light(briznaPos_, Vec3{1.0f, 0.8f, 0.5f} * (0.7f * pulse), 2.5f);
        glowDot(glow, view, briznaPos_, 0.16f * pulse, withAlpha(core, 0.18f));
        for (int k = 0; k < 2; ++k) {
            float a = time_ * 3.0f + float(k) * kPi;
            Vec3 p = briznaPos_ + Vec3{std::cos(a) * 0.16f, 0.05f * std::sin(a * 2.0f), std::sin(a) * 0.16f};
            glowStar(glow, view, p, 0.06f, time_ * 4.0f, withAlpha(rgba(255, 255, 255), 0.8f));
        }
    }
    // Fuente de luz señalada para viajar: un destello que la rodea.
    if (lightTarget_ >= 0 && solid)
        glowStar(glow, view, lightTargetPos(), 0.7f + 0.1f * std::sin(time_ * 8.0f), time_ * 2.0f, withAlpha(rgba(255, 245, 210), 0.8f));
    // Compañeros: la chispa aliada (cian) y el eco de Nora (un farolillo cálido).
    for (const Companion& m : companions_) {
        float fade = clampf(m.life / 1.0f, 0, 1);
        uint32_t col = m.kind == 1 ? rgba(255, 190, 110) : rgba(120, 230, 255);
        if (!solid) {
            dd.point(m.pos, col);
            continue;
        }
        if (m.kind == 1) {
            solid->ellipsoid(Mat4::identity(), m.pos, Vec3{0.17f, 0.22f, 0.17f} * fade, col, Material::Glow);
            solid->box(Mat4::identity(), m.pos + Vec3{0, 0.26f, 0}, Vec3{0.1f, 0.03f, 0.1f} * fade, rgba(240, 240, 250), Material::Metal);
            solid->light(m.pos, Vec3{1.0f, 0.75f, 0.45f} * (2.0f * fade), 6.0f);
            glowDot(glow, view, m.pos, 0.6f, withAlpha(col, 0.25f * fade));
        } else {
            solid->ellipsoid(Mat4::identity(), m.pos, Vec3{0.1f, 0.1f, 0.1f} * fade, col, Material::Glow);
            solid->light(m.pos, Vec3{0.5f, 0.9f, 1.0f} * (1.5f * fade), 4.0f);
            glowStar(glow, view, m.pos, 0.3f, time_ * 3.0f, withAlpha(col, 0.7f * fade));
        }
    }
    // Ecos de memoria: cristales blancos que giran con un halo irisado y un haz de luz.
    for (const MemoryEcho& m : echoes_) {
        if (!m.active || m.taken) continue;
        float s = smoothstep01(m.appear / 0.8f);
        Vec3 c = m.pos + Vec3{0, 0.08f * std::sin(time_ * 2.0f), 0};
        if (!solid) {
            dd.box(Mat4::identity(), c, {0.15f, 0.15f, 0.15f}, rgba(255, 255, 255));
            continue;
        }
        for (int k = 0; k < 3; ++k) {
            Mat4 t = translate(c) * rotateY(time_ * (0.8f + 0.3f * float(k)) + float(k) * 2.1f) * rotateX(0.6f * float(k));
            solid->box(t * rotateZ(kPi * 0.25f), {0.1f * float(k - 1), 0, 0}, Vec3{0.07f, 0.16f, 0.07f} * s,
                       rgba(250, 250, 255), Material::Glow);
        }
        uint32_t hue = mixRgb(rgba(120, 220, 255), rgba(255, 140, 220), 0.5f + 0.5f * std::sin(time_ * 1.5f));
        solid->light(c, Vec3{0.8f, 0.8f, 1.0f} * (1.5f * s), 4.0f);
        glowDot(glow, view, c, 0.6f * s, withAlpha(hue, 0.3f));
        glowQuad(glow, c + Vec3{0, 1.6f, 0}, view.right * 0.06f, Vec3{0, 1.6f, 0}, withAlpha(rgba(255, 255, 255), 0.18f * s));
    }
}

namespace {

// Parte un texto en líneas que caben en `maxW` píxeles (por palabras).
std::vector<std::string> wrapText(const std::string& text, float size, float maxW) {
    std::vector<std::string> lines;
    std::string cur, word;
    auto flushWord = [&] {
        if (word.empty()) return;
        std::string tryLine = cur.empty() ? word : cur + " " + word;
        if (!cur.empty() && textWidth(tryLine.c_str(), size) > maxW) {
            lines.push_back(cur);
            cur = word;
        } else {
            cur = tryLine;
        }
        word.clear();
    };
    for (char ch : text) {
        if (ch == ' ') flushWord();
        else word += ch;
    }
    flushWord();
    if (!cur.empty()) lines.push_back(cur);
    return lines;
}

uint32_t speakerColor(const std::string& id) {
    if (id == "brizna") return rgba(255, 228, 150);
    if (id == "prota") return rgba(255, 120, 205);
    if (id == "eco") return rgba(90, 225, 255);
    if (id == "sistema") return rgba(255, 196, 70);
    if (id == "nora") return rgba(255, 170, 90);
    if (id == "archivera") return rgba(200, 200, 225);
    return rgba(240, 240, 255);
}

}  // namespace

void Game::drawStoryHud(UiLayer& ui, const ViewInfo& view) const {
    DebugDraw& d = ui.shapes;
    const float w = float(view.width), h = float(view.height);
    if (fade_ > 0) fillRect(d, 0, 0, w, h, withAlpha(rgba(0, 0, 0), smoothstep01(fade_)));

    // Carteles de capítulo.
    if (chapterBanner_ > 0) {
        float total = chapterEnded_ ? 7.0f : 5.0f;
        float a = clampf(std::min((total - chapterBanner_) / 0.8f, chapterBanner_ / 1.0f), 0, 1);
        float y = h * 0.4f;
        fillRect(d, 0, y - 20, w, y + 110, withAlpha(rgba(8, 10, 30), 0.55f * a));
        uiText(ui, w * 0.5f, y, 22, chapterEnded_ ? "FIN DEL CAPÍTULO 1" : "CAPÍTULO 1", withAlpha(kUiCyan, a), FontFace::Title,
               Align::Center, 0.5f * a);
        uiText(ui, w * 0.5f, y + 34, 46, chapterEnded_ ? "Continuará…" : "La primera luz", withAlpha(kUiText, a),
               FontFace::Body, Align::Center, 0.6f * a);
    }

    const StoryLine* l = dialogue_.line();
    if (!l) return;
    std::string shown = utf8Prefix(l->text, dialogue_.visibleChars());
    std::string name = speakerName(l->speaker);
    uint32_t col = speakerColor(l->speaker);

    if (dialogue_.memory()) {
        // Recuerdo: la imagen se vela en blanco y la voz aparece en el centro.
        fillRect(d, 0, 0, w, h, withAlpha(rgba(235, 240, 255), 0.55f));
        fillRect(d, 0, h * 0.38f, w, h * 0.62f, withAlpha(rgba(20, 24, 60), 0.35f));
        std::vector<std::string> lines = wrapText(shown, 30, w * 0.6f);
        float y = h * 0.5f - float(lines.size()) * 20.0f;
        if (!name.empty()) uiText(ui, w * 0.5f, y - 34, 16, name.c_str(), withAlpha(col, 0.9f), FontFace::Title, Align::Center);
        for (const std::string& line : lines) {
            uiText(ui, w * 0.5f, y, 30, line.c_str(), rgba(255, 255, 255), FontFace::Body, Align::Center, 0.6f);
            y += 40;
        }
        return;
    }
    if (!dialogue_.blocking()) {
        // Aviso: subtítulo arriba, sin detener el juego.
        std::vector<std::string> lines = wrapText(shown, 21, 640);
        float y = 128;
        float bw = 700;
        fillRect(d, (w - bw) * 0.5f, y - 8, (w + bw) * 0.5f, y + 30 + float(lines.size()) * 28.0f, withAlpha(rgba(8, 10, 30), 0.6f));
        if (!name.empty()) uiText(ui, (w - bw) * 0.5f + 16, y - 2, 14, name.c_str(), col, FontFace::Title);
        y += 22;
        for (const std::string& line : lines) {
            uiText(ui, (w - bw) * 0.5f + 16, y, 21, line.c_str(), kUiText);
            y += 28;
        }
        return;
    }
    // Diálogo: caja abajo con el nombre en una pestaña y el texto letra a letra.
    const float bw = 880, bx = (w - bw) * 0.5f, by = h - 200, bh = 158;
    skewRect(d, bx, by, bx + bw, by + bh, 12, withAlpha(rgba(10, 14, 40), 0.9f));
    fillRect(d, bx + 12, by, bx + bw + 12, by + 3, col);
    if (!name.empty()) {
        float nw = textWidth(name.c_str(), 18, FontFace::Title) + 40;
        skewRect(d, bx + 20, by - 30, bx + 20 + nw, by, 8, col);
        uiText(ui, bx + 40, by - 26, 18, name.c_str(), rgba(14, 18, 46), FontFace::Title);
    }
    std::vector<std::string> lines = wrapText(shown, 25, bw - 80);
    float y = by + 26;
    for (const std::string& line : lines) {
        uiText(ui, bx + 40, y, 25, line.c_str(), l->speaker == "sistema" ? kUiAmber : kUiText);
        y += 34;
    }
    if (dialogue_.lineComplete() && std::fmod(time_, 0.8f) < 0.5f) {
        float tx = bx + bw - 30, ty = by + bh - 30;
        d.triangle({tx - 9, ty - 6, 0}, {tx + 9, ty - 6, 0}, {tx, ty + 5, 0}, col);
    }
    uiText(ui, bx + bw - 6, by + bh + 10, 14, "Clic / E / Espacio · continuar", kUiDim, FontFace::Body, Align::Right);
}

// --- HUD -------------------------------------------------------------------------------

void Game::drawHud(UiLayer& ui, const ViewInfo& view, bool debug) const {
    DebugDraw& d = ui.shapes;
    const float w = float(view.width), h = float(view.height);
    char buf[128];
    // En las escenas que detienen el juego (y al despertar) solo se ve la historia.
    if (dialogue_.blocking() || hero_.lying > 0.01f) {
        drawStoryHud(ui, view);
        return;
    }

    // Vitalidad abajo a la derecha: retrato hexagonal y barras de PV y PM inclinadas y
    // segmentadas (su longitud crece con el máximo).
    {
        float cx = w - 86, cy = h - 86;
        fillCircle(d, cx, cy, 58, kUiPanel, 6, kPi / 6);
        fillCircle(d, cx, cy, 52, hp_ / maxHp_ < 0.25f ? rgba(255, 60, 90) : kUiCyan, 6, kPi / 6);
        fillCircle(d, cx, cy, 47, rgba(20, 26, 64), 6, kPi / 6);
        fillCircle(d, cx, cy + 8, 26, rgba(250, 214, 188));  // cara
        fillCircle(d, cx, cy - 10, 27, rgba(232, 78, 176), 20);  // pelo
        for (int i = 0; i < 5; ++i) {
            float a = kPi * (1.05f + 0.22f * float(i));
            d.triangle({cx + std::cos(a) * 16 - 9, cy + std::sin(a) * 16 - 6, 0}, {cx + std::cos(a) * 16 + 9, cy + std::sin(a) * 16 - 6, 0},
                       {cx + std::cos(a) * 44, cy + std::sin(a) * 40 - 4, 0}, rgba(232, 78, 176));
        }
        fillRect(d, cx - 24, cy - 6, cx + 24, cy + 1, rgba(232, 78, 176));
        fillCircle(d, cx - 10, cy + 8, 5, kUiCyan, 10);
        fillCircle(d, cx + 10, cy + 8, 5, kUiCyan, 10);
        if (heroFlash_ > 0) fillCircle(d, cx, cy, 52, withAlpha(rgba(255, 60, 90), heroFlash_ * 2.5f), 6, kPi / 6);

        float right = cx - 66;
        uiText(ui, right - maxHp_ * 2.6f - 6, cy - 64, 15, "PV", kUiCyan, FontFace::Title);
        std::snprintf(buf, sizeof(buf), "%d / %d", int(std::ceil(hp_)), int(maxHp_));
        uiText(ui, right, cy - 66, 18, buf, kUiText, FontFace::Title, Align::Right);
        segBar(d, right, cy - 40, maxHp_ * 2.6f, 18, hp_ / maxHp_, hpGhost_ / maxHp_, rgba(70, 245, 190),
               rgba(255, 60, 120), int(maxHp_ / 10));
        segBar(d, right - 14, cy - 12, maxMp_ * 1.8f, 10, mp_ / maxMp_, mp_ / maxMp_, rgba(255, 225, 140), 0, 10);
        uiText(ui, right - 14 - maxMp_ * 1.8f - 12, cy - 16, 14, "LUZ", rgba(255, 230, 150), FontFace::Title, Align::Right);
        segBar(d, right - 24, cy + 8, maxShadow_ * 1.8f, 10, shadow_ / maxShadow_, shadow_ / maxShadow_, rgba(150, 80, 255), 0, 10);
        uiText(ui, right - 24 - maxShadow_ * 1.8f - 12, cy + 4, 14, "SOMBRA", rgba(180, 130, 255), FontFace::Title, Align::Right);
        if (mp_ >= maxMp_) {  // Supernova lista
            float pulse = 0.6f + 0.4f * std::sin(time_ * 6.0f);
            uiText(ui, right - 60, cy + 26, 15, "V · SUPERNOVA", withAlpha(rgba(255, 230, 150), pulse), FontFace::Title,
                   Align::Right, pulse);
        }
        if (lightAt(hero_.body.pos) > 0.3f)
            uiText(ui, right - 14 - maxMp_ * 1.8f - 60, cy - 16, 13, "EN LA LUZ", rgba(255, 245, 200), FontFace::Title,
                   Align::Right, 0.6f);
        // Forma activa: nombre y tiempo restante.
        if (form_ != Form::None) {
            uint32_t fc = formColor(form_);
            std::snprintf(buf, sizeof(buf), "%s  %.0f s", formName(form_), double(formTime_));
            uiText(ui, right, cy - 98, 16, buf, fc, FontFace::Title, Align::Right, 0.6f);
            segBar(d, right, cy - 78, 200, 6, formTime_ / 25.0f, formTime_ / 25.0f, fc, 0, 25);
        }
        // Lúmenes.
        diamond(d, w - 196, cy + 46, 9, kUiAmber);
        diamond(d, w - 196, cy + 46, 4, rgba(255, 250, 210));
        std::snprintf(buf, sizeof(buf), "%d", inv_.munny);
        uiText(ui, w - 180, cy + 34, 22, buf, kUiAmber, FontFace::Title);
        uiText(ui, w - 180 + textWidth(buf, 22, FontFace::Title) + 8, cy + 40, 14, "LUM", kUiDim, FontFace::Title);
    }

    // Menú de comandos abajo a la izquierda: magias (1-3) y atajos de objetos (4-6).
    {
        const float x = 28, rowH = 34, width = 300;
        float y0 = h - 30 - rowH * float(kCommandCount) - 40;
        skewRect(d, x, y0, x + 170, y0 + 30, 10, rgba(40, 220, 255, 230));
        uiText(ui, x + 16, y0 + 6, 17, "COMANDOS", rgba(10, 20, 50), FontFace::Title);
        for (int i = 0; i < kCommandCount; ++i) {
            float y = y0 + 36 + float(i) * rowH;
            bool sel = i == commandSel_;
            char name[64], value[32];
            bool usable;
            uint32_t accent;
            if (i < 3) {
                const SpellDef& s = spellDef(Spell(i));
                std::snprintf(name, sizeof(name), "%s", s.name);
                std::snprintf(value, sizeof(value), "%d luz", int(s.cost));
                usable = mp_ >= s.cost;
                accent = s.color;
            } else {
                ItemId id = inv_.quick[i - 3];
                std::snprintf(name, sizeof(name), "%s", itemName(id));
                std::snprintf(value, sizeof(value), "×%d", inv_.count(id));
                usable = inv_.count(id) > 0;
                accent = itemDef(id).color;
            }
            float x0 = x + (sel ? 16.0f : 0.0f);
            skewRect(d, x0, y, x0 + width, y + rowH - 5, 8, sel ? rgba(235, 245, 255, 240) : kUiPanel);
            skewRect(d, x0, y, x0 + 6, y + rowH - 5, 8, usable ? accent : rgba(90, 95, 120));
            if (sel) d.triangle({x0 - 14, y + 5, 0}, {x0 - 14, y + rowH - 10, 0}, {x0 - 4, y + (rowH - 5) * 0.5f, 0}, kUiCyan);
            char key[4];
            std::snprintf(key, sizeof(key), "%d", i + 1);
            uint32_t tc = !usable ? rgba(120, 125, 150) : sel ? rgba(20, 30, 70) : kUiText;
            uiText(ui, x0 + 18, y + 6, 15, key, sel ? rgba(40, 120, 170) : kUiCyan, FontFace::Title);
            uiText(ui, x0 + 40, y + 4, 19, name, tc);
            uiText(ui, x0 + width - 10, y + 6, 15, value, sel ? rgba(60, 70, 110) : kUiDim, FontFace::Title, Align::Right);
        }
        const char* help = inLight_ ? "DENTRO DE LA LUZ · Q saltar a la fuente señalada · R elegir fuente · Clic emboscada · Espacio salir"
                           : lock_ >= 0 ? "Clic atacar · R soltar objetivo · Clic central cambiar de objetivo · C esquivar · Shift correr"
                                        : "Clic atacar · Q junto a una farola: entrar en la luz · R fijar · V supernova · G forma · T compañero · C esquivar";
        skewRect(d, x - 6, h - 30, x + 20 + textWidth(help, 14), h - 8, 6, kUiPanel);
        uiText(ui, x, h - 28, 14, help, kUiDim);
    }

    // Objetivo fijado arriba a la derecha.
    if (lock_ >= 0) {
        const Enemy& e = enemies_[size_t(lock_)];
        const EnemyDef& ed = e.def();
        float right = w - 34;
        uiText(ui, right, 22, 22, e.name(), kUiPink, FontFace::Title, Align::Right, 0.5f);
        float f = std::max(0.0f, e.health) / (ed.maxHealth * (e.boss ? 7.0f : 1.0f));
        segBar(d, right, 56, 240, 12, f, f, kUiPink, 0, 10);
    }

    if (novaFlash_ > 0) fillRect(d, 0, 0, w, h, withAlpha(rgba(255, 250, 235), novaFlash_ * 0.9f));
    // Jefe: barra grande arriba en el centro.
    for (const Enemy& e : enemies_)
        if (e.boss && e.alive()) {
            float f = std::max(0.0f, e.health) / (e.def().maxHealth * 7.0f);
            uiText(ui, w * 0.5f, 16, 20, e.name(), rgba(255, 170, 90), FontFace::Title, Align::Center, 0.5f);
            segBar(d, w * 0.5f + 260, 44, 520, 14, f, f, rgba(255, 150, 80), 0, 20);
        }
    // Avisos de objetos obtenidos (a la derecha, entran deslizándose y se desvanecen).
    for (size_t i = 0; i < notices_.size(); ++i) {
        const Notice& n = notices_[i];
        float a = clampf(std::min(n.age / 0.15f, (4.0f - n.age) / 0.6f), 0, 1);
        float slide = (1.0f - smoothstep01(n.age / 0.25f)) * 80.0f;
        float tw = textWidth(n.text.c_str(), 19);
        float y = 96 + float(i) * 40, x1 = w - 30 + slide, x0 = x1 - tw - 52;
        skewRect(d, x0, y, x1, y + 32, 8, withAlpha(rgba(12, 16, 42), 0.85f * a));
        diamond(d, x0 + 22, y + 16, 8, withAlpha(n.color, a));
        uiText(ui, x0 + 38, y + 5, 19, n.text.c_str(), withAlpha(n.color, a));
    }

    // Escenario, parte y objetivo arriba a la izquierda, con el croma del escenario.
    {
        const LevelPart* p = currentPart();
        skewRect(d, 18, 14, 380, 96, 10, kUiPanel);
        fillRect(d, 18, 14, 22, 96, kUiCyan);
        uiText(ui, 30, 18, 15, levelName_.c_str(), kUiCyan, FontFace::Title);
        if (p) {
            std::snprintf(buf, sizeof(buf), "Parte %d/%d · %s", int(part_) + 1, int(parts_.size()), p->title.c_str());
            uiText(ui, 30, 40, 17, buf, kUiText);
            std::string obj = "› " + p->objective;
            if (p->kind == PartKind::Nodes || p->kind == PartKind::Lanterns) {
                int n = 0;
                if (p->kind == PartKind::Nodes)
                    for (const ColorNode& c : nodes_) n += c.lit;
                else
                    for (const Lantern& l : lanterns_) n += l.lit;
                obj += " (" + std::to_string(n) + "/" + std::to_string(p->count) + ")";
            }
            uiText(ui, 30, 66, 16, obj.c_str(), kUiAmber);
        } else {
            uiText(ui, 30, 44, 17, "Escenario completado", rgba(110, 255, 190));
        }
        // Croma: barra arcoíris (cada segmento de un color, apagados los que faltan).
        skewRect(d, 18, 102, 250, 140, 10, kUiPanel);
        std::snprintf(buf, sizeof(buf), "CROMA %d%%", int(cromaShown_ * 100.0f + 0.5f));
        uiText(ui, 30, 105, 14, buf, kUiText, FontFace::Title);
        const uint32_t rainbow[] = {rgba(255, 70, 120), rgba(255, 150, 50), rgba(255, 220, 60), rgba(110, 255, 140),
                                    rgba(40, 220, 255), rgba(120, 110, 255), rgba(230, 90, 255)};
        const int segs = 14;
        for (int i = 0; i < segs; ++i) {
            float x0 = 30 + float(i) * 15.0f, lit = clampf(cromaShown_ * segs - float(i), 0, 1);
            uint32_t c = rainbow[i * 7 / segs];
            skewRect(d, x0, 125, x0 + 12, 133, 3, rgba(50, 50, 70, 220));
            if (lit > 0) skewRect(d, x0, 125, x0 + 12 * lit, 133, 3, c);
        }
        // Cartel con el nombre de la parte al empezarla.
        float by = h * 0.2f;
        if (partBanner_ > 0 && p) {
            float a = clampf(std::min((4.0f - partBanner_) / 0.4f, partBanner_ / 0.6f), 0, 1);
            fillRect(d, 0, by - 14, w, by + 92, withAlpha(rgba(12, 16, 42), 0.5f * a));
            uiText(ui, w * 0.5f, by - 4, 16, levelName_.c_str(), withAlpha(kUiCyan, a), FontFace::Title, Align::Center);
            uiText(ui, w * 0.5f, by + 18, 44, p->title.c_str(), withAlpha(kUiText, a), FontFace::Body, Align::Center, 0.6f * a);
        }
        // Indicador del objetivo: rombo sobre su posición o, si está fuera de la pantalla,
        // flecha en el borde hacia él.
        if (p && hero_.downTimer < 0 && testMode_ != TestMode::Combat) {
            float sx, sy;
            Vec3 m = p->marker;
            if (p->kind == PartKind::Nodes)
                for (const ColorNode& c : nodes_)
                    if (!c.lit) { m = c.pos + Vec3{0, 2.6f, 0}; break; }
            if (p->kind == PartKind::Lanterns)
                for (const Lantern& l : lanterns_)
                    if (!l.lit) { m = l.pos + Vec3{0, 2.6f, 0}; break; }
            float dist = length(m - hero_.body.pos);
            bool on = project(view, m, sx, sy) && sx > 40 && sx < w - 40 && sy > 40 && sy < h - 40;
            if (!on) {
                if (!project(view, m, sx, sy)) {  // detrás de la cámara: al borde inferior
                    sx = w - sx;
                    sy = h - 40;
                }
                sx = clampf(sx, 40, w - 40);
                sy = clampf(sy, 160, h - 40);
            }
            float pulse = 1.0f + 0.15f * std::sin(time_ * 5.0f);
            diamond(d, sx, sy, 11 * pulse, withAlpha(kUiAmber, 0.9f));
            diamond(d, sx, sy, 5, rgba(255, 250, 220));
            std::snprintf(buf, sizeof(buf), "%.0f m", double(dist));
            uiText(ui, sx, sy + 14, 14, buf, kUiAmber, FontFace::Title, Align::Center);
        }
    }

    // Fuente de luz señalada: se puede viajar hasta ella con Q.
    if (lightTarget_ >= 0 && hero_.downTimer < 0) {
        float sx, sy;
        Vec3 lp = lightTargetPos();
        if (project(view, lp, sx, sy)) {
            float pulse = 1.0f + 0.12f * std::sin(time_ * 8.0f);
            fillCircle(d, sx, sy, 22 * pulse, withAlpha(rgba(255, 240, 190), 0.25f), 20);
            fillCircle(d, sx, sy, 6, rgba(255, 250, 225), 12);
            if (inLight_)
                std::snprintf(buf, sizeof(buf), "Q · saltar  %.0f m", double(length(lp - hostPos_)));
            else
                std::snprintf(buf, sizeof(buf), "Q · entrar en la luz");
            uiText(ui, sx, sy + 26, 15, buf, rgba(255, 240, 190), FontFace::Body, Align::Center, 0.4f);
        }
    }
    // Dentro de la luz: el Gris sobre el que caería la emboscada.
    if (int a = ambushTarget(); a >= 0) {
        float sx, sy;
        if (project(view, enemies_[size_t(a)].center() + Vec3{0, 1.0f, 0}, sx, sy)) {
            float pulse = 1.0f + 0.12f * std::sin(time_ * 10.0f);
            diamond(d, sx, sy, 12 * pulse, withAlpha(kUiPink, 0.85f));
            uiText(ui, sx, sy + 16, 15, "Clic · emboscada", kUiPink, FontFace::Body, Align::Center, 0.4f);
        }
    }
    InteractTarget it = nearbyInteract();
    if (it.kind >= 0 && hero_.downTimer < 0 && !inLight_) {
        const char* msg = it.label.c_str();
        float tw = textWidth(msg, 22) + 50;
        float x0 = (w - tw) * 0.5f, y0 = h * 0.7f;
        skewRect(d, x0, y0, x0 + tw, y0 + 38, 10, kUiPanel);
        fillRect(d, x0 + 12, y0 + 7, x0 + 36, y0 + 31, kUiCyan);
        uiText(ui, x0 + 24, y0 + 8, 18, "F", rgba(10, 20, 50), FontFace::Title, Align::Center);
        uiText(ui, x0 + 46, y0 + 7, 22, msg, kUiText);
    }
    if (hero_.downTimer >= 0) {
        float a = clampf(hero_.downTimer / 0.8f, 0, 1);
        fillRect(d, 0, 0, w, h, withAlpha(rgba(20, 10, 40), 0.45f * a));
        uiText(ui, w * 0.5f, h * 0.38f, 56, "SEÑAL PERDIDA", withAlpha(kUiPink, a), FontFace::Title, Align::Center, a);
        uiText(ui, w * 0.5f, h * 0.38f + 72, 22, "Reconstruyendo el prisma...", withAlpha(kUiDim, a), FontFace::Body,
               Align::Center);
    }

    // Modo debug: estado de la IA y vida encima de cada enemigo, y recuentos.
    if (debug) {
        for (const Enemy& e : enemies_) {
            float sx, sy;
            if (!project(view, e.center() + Vec3{0, e.def().height * 0.5f + 0.3f, 0}, sx, sy)) continue;
            std::snprintf(buf, sizeof(buf), "%s · %s %.0f", aiStateName(e.state), intentName(e.intent),
                          double(std::max(0.0f, e.health)));
            uiText(ui, sx, sy, 15, buf, kUiAmber, FontFace::Body, Align::Center);
        }
        std::snprintf(buf, sizeof(buf), "DEBUG · enemigos %zu · botín %zu · partículas %zu · proyectiles %zu",
                      enemies_.size(), pickups_.size(), particles_.size(), projectiles_.size());
        uiText(ui, 28, 74, 15, buf, rgba(120, 255, 160));
        uiText(ui, 28, 94, 15, "F1 huesos · F2 articulaciones · F3 cajas · F4 rejilla", rgba(120, 200, 160));
    }
    drawStoryHud(ui, view);
}
