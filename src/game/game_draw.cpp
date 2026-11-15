#include "game/game.h"

#include "render/renderer.h"
#include "render/models.h"
#include "render/text.h"

#include <cstdio>

namespace {

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
const uint32_t kHair = rgba(74, 72, 76), kScarfC = rgba(104, 58, 50), kEyeC = rgba(36, 34, 38);
const uint32_t kVein = rgba(255, 222, 170), kOutline = rgba(14, 14, 18), kGhostWhite = rgba(255, 238, 214);
// clang-format off
const Shape kHeroShapes[] = {
    {Chest,     {0, 0.235f, 0.1f},        {0.06f, 0.035f, 0.02f},   kScarfC, Material::Cloth},  // nudo de la bufanda
    {Chest,     {0.012f, 0.205f, 0.112f}, {0.03f, 0.05f, 0.012f},   kScarfC, Material::Cloth},  // y su caída
};
// Vetas de luz bajo la piel y el abrigo: brillan según la Luz que lleva (apagadas, grises).
struct NeonLine {
    int joint;
    Vec3 center, half;
};
const NeonLine kNeonLines[] = {
    {Chest,  {0.0f, 0.06f, 0.124f},   {0.004f, 0.07f, 0.004f}},   // en la abertura del abrigo
    {WristL, {0, -0.03f, 0.05f},      {0.004f, 0.04f, 0.004f}},   // dorso de las manos
    {WristR, {0, -0.03f, 0.05f},      {0.004f, 0.04f, 0.004f}},
    {Neck,   {0.03f, 0.06f, 0.03f},   {0.003f, 0.03f, 0.003f}},
};
// clang-format on
// Rostro del protagonista, estilo de animación: ojos grandes pintados sobre la cara (blanco,
// iris de color con su brillo y una línea de pestañas marcada), de dos colores (ámbar, el de
// la luz; violeta, el de la sombra); parpadean y la mirada se escapa a veces. Cejas finas,
// una boca pequeña con media sonrisa y, bajo el ojo izquierdo, una veta de luz como una
// lágrima que brilla con la Luz (violeta en modo sombra).
void drawHeroFace(Renderer& r, const Mat4* j, Vec3 /*lag*/, float flash, uint32_t vein, bool veinGlow, float time, HeroLook look) {
    const Mat4& h = j[Head];
    float bp = std::fmod(time + 1.3f * std::sin(time * 0.17f), 4.1f);
    float blink = bp < 0.14f ? std::sin(bp / 0.14f * kPi) : 0.0f;
    float glance = std::sin(time * 0.31f) > 0.93f ? 0.0035f : std::sin(time * 0.53f) > 0.95f ? -0.0035f : 0.0f;
    const uint32_t skin = rgba(238, 204, 178), lash = rgba(38, 30, 36);
    // Dónde está la piel delante de cada rasgo (se calcula una vez sobre la forma de la cabeza).
    static const Vec3 eyeAt[2] = {headSurface(0.038f, 0.118f), headSurface(-0.038f, 0.118f)};
    static const Vec3 browAt[2] = {headSurface(0.04f, 0.155f), headSurface(-0.04f, 0.155f)};
    static const Vec3 mouthAt = headSurface(0.0f, 0.063f);
    static const Vec3 tear[3] = {headSurface(0.05f, 0.094f), headSurface(0.054f, 0.082f), headSurface(0.05f, 0.071f)};
    for (float sx : {1.0f, -1.0f}) {
        int side = sx > 0 ? 0 : 1;
        Mat4 e = h * translate(eyeAt[side] - Vec3{0, 0, 0.0025f}) * rotateY(0.34f * sx) * rotateX(-0.08f);
        r.ellipsoid(e, {}, {0.0205f, 0.0245f, 0.0062f}, rgba(246, 242, 236), Material::Cloth, flash);
        // Un ojo de cada color; en cada modo, los dos del suyo.
        uint32_t iris = look == HeroLook::Light ? rgba(240, 176, 60) : look == HeroLook::Shadow ? rgba(168, 96, 255)
                        : sx > 0                ? rgba(226, 164, 66)
                                                : rgba(150, 126, 206);
        Vec3 io{glance - 0.002f * sx, -0.003f, 0.0012f};
        r.ellipsoid(e, io, {0.0135f, 0.0175f, 0.0062f}, iris, Material::Cloth, flash);
        r.ellipsoid(e, io + Vec3{0, 0.004f, 0.0006f}, {0.0128f, 0.0075f, 0.0062f}, mixRgb(iris, rgba(40, 30, 40), 0.35f),
                    Material::Cloth, flash);  // sombra del párpado sobre el iris
        r.ellipsoid(e, io + Vec3{0, -0.001f, 0.0012f}, {0.0062f, 0.0088f, 0.0062f}, rgba(24, 18, 26), Material::Cloth, flash);
        r.ellipsoid(e, io + Vec3{0.0045f * sx, 0.0065f, 0.0022f}, {0.0042f, 0.0052f, 0.006f}, rgba(255, 255, 255), Material::Glow, flash);
        // Párpado al parpadear y línea de pestañas (más gruesa hacia fuera).
        if (blink > 0.01f)
            r.ellipsoid(e, {0, 0.024f * (1.0f - blink), 0.0012f}, {0.0222f, 0.026f * blink + 0.002f, 0.0068f}, skin, Material::Cloth, flash);
        float ly = 0.0215f - 0.042f * blink;
        r.ellipsoid(e * translate({0.002f * sx, ly, 0.0012f}) * rotateZ(-0.18f * sx), {}, {0.0235f, 0.0042f, 0.0068f}, lash,
                    Material::Cloth, flash);
        r.ellipsoid(e * translate({0.0215f * sx, ly - 0.004f, 0.0012f}) * rotateZ(-0.6f * sx), {}, {0.007f, 0.0026f, 0.0066f}, lash,
                    Material::Cloth, flash);
        // Ceja fina, algo arqueada.
        r.ellipsoid(h * translate(browAt[side] - Vec3{0, 0, 0.002f}) * rotateY(0.34f * sx) * rotateZ(0.12f * sx), {},
                    {0.021f, 0.0034f, 0.0045f}, heroLookColor(look, rgba(50, 44, 52)), Material::Cloth, flash);
    }
    // Boca pequeña con media sonrisa.
    r.ellipsoid(h * translate(mouthAt + Vec3{-0.002f, 0, -0.0018f}), {}, {0.0105f, 0.0016f, 0.0035f}, rgba(150, 92, 92),
                Material::Cloth, flash);
    r.ellipsoid(h * translate(mouthAt + Vec3{0.0095f, 0.0015f, -0.0025f}) * rotateZ(0.5f), {}, {0.0035f, 0.0014f, 0.0034f},
                rgba(150, 92, 92), Material::Cloth, flash);
    // La veta de luz bajo el ojo izquierdo.
    uint32_t tearCol = mixRgb(vein, skin, 0.35f);  // tenue: que no se convierta en un destello
    for (int i = 0; i < 2; ++i) {
        Vec3 d = tear[i + 1] - tear[i];
        r.box(h * translate((tear[i] + tear[i + 1]) * 0.5f) * basisY(normalize(d)), {}, {0.0012f, length(d) * 0.5f + 0.001f, 0.0015f},
              tearCol, veinGlow ? Material::Glow : Material::Cloth, flash);
    }
}
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
    const uint32_t grip = ws.hilt;
    if (ws.style == WeaponStyle::Fists) {  // guantelete: funda clara y nudillos de luz
        r.ellipsoid(w, {0, -0.06f, 0.008f}, {0.062f, 0.088f, 0.072f}, ws.blade, Material::Metal);
        r.ellipsoid(w, {0, -0.115f, 0.03f}, {0.055f, 0.022f, 0.05f}, ws.trail, Material::Glow);
        r.ellipsoid(w, {0, 0.01f, 0}, {0.06f, 0.025f, 0.06f}, ws.trail, Material::Glow);  // puño
        return;
    }
    if (ws.style == WeaponStyle::Claws) {  // garras: guante negro y tres cuchillas de sombra
        r.ellipsoid(w, {0, -0.06f, 0.008f}, {0.06f, 0.086f, 0.07f}, ws.blade, Material::Metal);
        for (int k = -1; k <= 1; ++k) {
            Mat4 cl = w * translate({0.028f * float(k), -0.1f, 0.06f}) * rotateY(0.12f * float(k)) * rotateX(0.35f);
            r.box(cl, {0, 0, 0.09f}, {0.007f, 0.004f, 0.1f}, ws.trail, Material::Glow);
            r.box(cl, {0, 0.004f, 0.08f}, {0.009f, 0.003f, 0.09f}, ws.blade, Material::Metal);
        }
        return;
    }
    Vec3 shaftBase = kBladeBase, shaftTip = kBladeBase + Vec3{0, 0, len};
    if (ws.style == WeaponStyle::Hammer) {  // mazo: mango con vendas y cabeza enorme con vetas de sombra
        r.box(w, (shaftBase + shaftTip) * 0.5f - Vec3{0, 0, 0.1f}, {0.022f, 0.022f, len * 0.5f + 0.1f}, grip, Material::Cloth);
        for (int k = 0; k < 3; ++k)
            r.box(w, shaftBase + Vec3{0, 0, len * (0.15f + 0.25f * float(k))}, {0.026f, 0.026f, 0.012f}, ws.blade, Material::Metal);
        Vec3 head = shaftTip + Vec3{0, 0, 0.06f};
        r.box(w, head, {0.19f, 0.11f, 0.11f}, ws.blade, Material::Stone);
        r.box(w, head + Vec3{0.2f, 0, 0}, {0.012f, 0.09f, 0.09f}, ws.trail, Material::Glow);  // caras que golpean
        r.box(w, head - Vec3{0.2f, 0, 0}, {0.012f, 0.09f, 0.09f}, ws.trail, Material::Glow);
        r.box(w, head, {0.195f, 0.02f, 0.115f}, ws.trail, Material::Glow);  // veta
        return;
    }
    if (ws.style == WeaponStyle::Scythe) {  // guadaña: asta larga y una hoja curva que sale hacia el filo
        r.box(w, (shaftBase + shaftTip) * 0.5f - Vec3{0, 0, 0.15f}, {0.017f, 0.017f, len * 0.5f + 0.15f}, grip, Material::Metal);
        const int n = 7;
        Vec3 p = shaftTip;
        for (int i = 0; i < n; ++i) {
            float t = float(i + 1) / float(n);
            Vec3 q = shaftTip + Vec3{0.62f * t, 0, -0.32f * t * t};
            Vec3 dir = normalize(q - p);
            Mat4 sm = w * translate((p + q) * 0.5f) * rotateY(std::atan2(dir.x, dir.z));
            float wi = lerpf(0.06f, 0.012f, t);
            r.box(sm, {wi * 0.5f, 0, 0}, {wi, 0.009f, length(q - p) * 0.55f}, ws.blade, Material::Metal);
            r.box(sm, {-wi * 0.4f, 0, 0}, {0.005f, 0.011f, length(q - p) * 0.52f}, ws.trail, Material::Glow);
            p = q;
        }
        return;
    }
    r.box(w, kGripLocal, {0.02f, 0.02f, 0.075f}, grip, Material::Metal);
    r.box(w, kGripLocal + Vec3{0, 0, 0.09f}, {0.07f, 0.014f, 0.016f}, ws.blade, Material::Metal);  // guarda
    r.ellipsoid(w, kGripLocal - Vec3{0, 0, 0.08f}, {0.025f, 0.025f, 0.025f}, ws.blade, Material::Metal);  // pomo

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
    // Hoja: núcleo luminoso del color del prisma dentro de una funda clara. Los sables se
    // arquean (tramos que se van girando hacia el lomo) y cada espada tiene su anchura.
    float bw = ws.width;
    if (ws.curve > 0.001f) {
        const int n = 6;
        float seg = len / float(n);
        Vec3 p = base;
        for (int i = 0; i < n; ++i) {
            float t0 = float(i) / float(n), t1 = float(i + 1) / float(n);
            Vec3 q = base + Vec3{ws.curve * t1 * t1, 0, len * t1};  // se arquea en el plano del filo
            Vec3 dir = normalize(q - p);
            Mat4 sm = w * translate((p + q) * 0.5f) * rotateY(std::atan2(dir.x, dir.z));
            float wi = bw * lerpf(1.0f, 0.75f, t0);
            r.box(sm, {}, {wi, 0.011f, seg * 0.52f}, ws.blade, Material::Metal);
            r.box(sm, {-wi * 0.8f, 0, 0}, {0.004f, 0.013f, seg * 0.5f}, ws.trail, Material::Glow);  // luz por el filo
            p = q;
        }
        tip = p;
        r.ellipsoid(w, tip, {bw * 0.75f, 0.011f, 0.06f}, ws.blade, Material::Metal);
    } else {
        r.box(w, mid, {bw, 0.011f, len * 0.5f}, ws.blade, Material::Metal);
        if (ws.shadow)
            r.box(w, mid, {0.004f, 0.013f, len * 0.46f}, ws.trail, Material::Glow);  // veta de sombra por el filo
        else  // las de la luz: el núcleo de luz ocupa casi toda la hoja
            r.box(w, mid, {bw * 0.6f, 0.0125f, len * 0.47f}, ws.trail, Material::Glow);
        r.ellipsoid(w, tip, {bw, 0.011f, 0.08f}, ws.blade, Material::Metal);
    }
    if (bw >= 0.05f)  // espadas anchas: acanaladura oscura a lo largo
        r.box(w, mid - Vec3{0, 0, len * 0.08f}, {0.006f, 0.012f, len * 0.3f}, ws.hilt, Material::Metal);
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

}  // namespace

// --- Escena -------------------------------------------------------------------------------

void Game::draw(DebugDraw& dd, DebugDraw& glow, Renderer* solid, const DrawFlags& f, const ViewInfo& view,
                float alpha) const {
    drawLevel(dd, solid, f, view, alpha);
    drawHero(dd, solid, f, alpha);
    drawGhosts(solid, dd, alpha);
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
    // Oscuridad: en cuanto la sombra ha despertado se ve con claridad qué está a oscuras
    // (más aún en modo sombra, que solo puede moverse por ahí).
    solid->setDarkness(darkShown_);
    if (darkShown_ > 0) {
        for (const Lamp& l : lamps_)
            if (l.on && l.bulb) solid->litArea(l.pos, lampRadius(l));
        for (const ColorNode& n : nodes_)
            if (n.lit) solid->litArea(n.pos, n.radius * smoothstep01(n.litTime / 3.0f));
        for (const Lantern& ln : lanterns_)
            if (ln.lit) solid->litArea(ln.pos, 6.0f);
        if (inLight_) solid->litArea(hostPos_, 5.0f);
    }
    solid->setScenery(true);
    // Charcos de oscuridad (antes que las salpicaduras: tienen prioridad en el límite).
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
    for (const Interactable& it : interacts_) {
        if (it.used) continue;
        solid->light(it.pos + Vec3{0, 1.5f, 0}, Vec3{1.0f, 0.8f, 0.5f} * (0.8f + 0.4f * std::sin(time_ * 3.0f)), 3.0f);
        if (it.id == "hoja") {  // la hoja, tirada en el suelo
            Mat4 m = translate(it.pos + Vec3{0, 0.03f, 0}) * rotateY(0.6f);
            solid->box(m, {0, 0, 0.3f}, {0.04f, 0.011f, 0.4f}, rgba(118, 120, 126), Material::Metal);
            solid->box(m, {0, 0, -0.17f}, {0.02f, 0.02f, 0.08f}, rgba(70, 60, 52), Material::Cloth);
            solid->box(m, {0, 0, -0.08f}, {0.07f, 0.014f, 0.016f}, rgba(118, 120, 126), Material::Metal);
        }
    }
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
    for (size_t i = 0; i < lamps_.size(); ++i) {
        const Lamp& l = lamps_[i];
        uint32_t bulb = l.color;
        if (l.on && l.energy < 0.5f)  // queda poca luz: parpadea
            bulb = mixRgb(l.color, rgba(40, 30, 60), (0.5f - l.energy) * (1.2f + 0.8f * std::sin(time_ * 25.0f + float(i))));
        if (l.bulb && l.broken) {  // rota: solo queda el casquillo y unos dientes de cristal
            solid->ellipsoid(translate(l.pos + Vec3{0, 0.1f, 0}), {}, {0.12f, 0.08f, 0.12f}, rgba(40, 40, 46), Material::Metal);
            for (int k = 0; k < 5; ++k) {
                float a = float(k) * 1.257f + float(i);
                solid->box(translate(l.pos + Vec3{std::cos(a) * 0.09f, 0.0f, std::sin(a) * 0.09f}) * rotateY(a) * rotateZ(0.4f), {},
                           {0.012f, 0.06f, 0.025f}, rgba(150, 160, 175), Material::Metal);
            }
        } else if (l.bulb) {
            solid->ellipsoid(translate(l.pos), {}, {0.2f, 0.2f, 0.2f}, l.on ? bulb : rgba(70, 72, 80),
                             l.on ? Material::Glow : Material::Metal, l.hitFlash > 0 ? 0.7f : 0.0f);
        }
        if (l.on && l.bulb && length(flat(l.pos - hero_.body.pos)) < 14.0f) {
            float r = float((l.color >> 0) & 0xFF) / 255.0f, g = float((l.color >> 8) & 0xFF) / 255.0f,
                  b = float((l.color >> 16) & 0xFF) / 255.0f;
            solid->light(l.pos, Vec3{0.4f + r * 0.6f, 0.4f + g * 0.6f, 0.4f + b * 0.6f} * 1.3f, 5.0f);
        }
    }

    // Interruptores de las farolas: poste corto con una caja y su palanca; la luz del panel
    // dice si sus farolas están encendidas (cálida) o apagadas (violeta).
    for (const LampSwitch& s : switches_) {
        Mat4 m = translate(s.pos);
        solid->box(m, {0, 0.6f, 0}, {0.05f, 0.6f, 0.05f}, rgba(60, 62, 70), Material::Metal);
        solid->box(m, {0, 1.25f, 0}, {0.16f, 0.2f, 0.09f}, rgba(46, 48, 56), Material::Metal);
        uint32_t lc = s.on ? rgba(255, 220, 140) : rgba(160, 90, 255);
        solid->box(m, {0, 1.4f, 0.095f}, {0.11f, 0.025f, 0.01f}, lc, Material::Glow);
        float lever = (s.on ? -0.6f : 0.6f) * smoothstep01(s.flip / 0.15f) + (s.on ? 0.6f : -0.6f) * (1.0f - smoothstep01(s.flip / 0.15f));
        solid->box(m * translate({0, 1.2f, 0.1f}) * rotateX(lever), {0, 0.08f, 0}, {0.02f, 0.09f, 0.02f}, rgba(200, 200, 210),
                   Material::Metal);
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
    if (diving_) {  // fundido con la sombra: una mancha oscura en el suelo, con un filo violeta
        Vec3 c = hero_.interpolatedPos(alpha) + Vec3{0, 0.04f, 0};
        float pulse = 1.0f + 0.06f * std::sin(time_ * 7.0f);
        Mat4 m = translate(c) * rotateY(hero_.yaw);
        solid->ellipsoid(m, {}, Vec3{0.55f, 0.04f, 0.75f} * pulse, rgba(10, 8, 16), Material::Cloth);
        solid->ellipsoid(m, {0, 0.03f, 0}, Vec3{0.3f, 0.03f, 0.45f} * pulse, rgba(150, 80, 255), Material::Glow);
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
    // Aspecto según el modo. Al cambiar, el cuerpo se enciende de blanco y cambia de ropa en
    // el momento más brillante.
    HeroLook look = !shadowUnlocked_ ? HeroLook::Base : shadowMode_ ? HeroLook::Shadow : HeroLook::Light;
    if (modeFlash_ > 0 && shadowUnlocked_) {
        float t = modeFlash_ / kModeSwapTime;  // 1 → 0
        if (t > 0.5f) look = shadowMode_ ? HeroLook::Light : HeroLook::Shadow;
        flash = std::max(flash, 1.0f - std::fabs(t - 0.5f) * 2.0f);
    }
    uint32_t outline = look == HeroLook::Light ? rgba(150, 108, 36) : look == HeroLook::Shadow ? rgba(70, 24, 130) : kOutline;
    // Cuerpo continuo (malla con skinning) con contorno, y los detalles rígidos encima.
    const BuiltModel& body = heroModel(look, false);
    Mat4 skin[kMaxJoints];
    skinMatrices(body, j, skin);
    solid->skinned(body.mesh, skin, body.jointCount, flash, form_ != Form::None ? formColor(form_) : outline);
    // La cabeza va en su propia malla, más fina (misma postura de reposo: mismas matrices).
    // Sin contorno: en los huecos tallados (ojos, boca) el casco invertido asomaría por delante.
    const BuiltModel& head = heroModel(look, true);
    solid->skinned(head.mesh, skin, head.jointCount, flash, 0);
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
    for (const Shape& s : kHeroShapes) solid->ellipsoid(j[s.joint], s.center, s.radii, heroLookColor(look, s.color), s.material, flash);
    // Las vetas: apagadas sin Luz; encendidas, del color de la luz (o de la sombra).
    float lit = clampf(mp_ / maxMp_, 0, 1);
    uint32_t vein = mixRgb(rgba(70, 68, 72), shadowMode_ ? rgba(170, 100, 255) : kVein, lit);
    for (const NeonLine& l : kNeonLines) solid->box(j[l.joint], l.center, l.half, vein, lit > 0.05f ? Material::Glow : Material::Cloth, flash);
    // Mechones de pelo: se inclinan hacia donde se queda atrás el punto con muelle que
    // sigue a la cabeza (inercia al correr, saltar y girar).
    Vec3 lagW = hero_.hairLag();
    const float* hm = j[Head].m;
    Vec3 lag{hm[0] * lagW.x + hm[1] * lagW.y + hm[2] * lagW.z, hm[4] * lagW.x + hm[5] * lagW.y + hm[6] * lagW.z,
             hm[8] * lagW.x + hm[9] * lagW.y + hm[10] * lagW.z};
    drawHeroFace(*solid, j, lag, flash, vein, lit > 0.05f, time_, look);
    // Correa de cuero cruzada sobre el pecho (de la vaina de la hoja), siguiendo el abrigo.
    for (int k = 0; k < 6; ++k) {
        float t0 = float(k) / 6.0f, t1 = float(k + 1) / 6.0f;
        auto onChest = [](float t) {
            float x = lerpf(0.13f, -0.13f, t), y = lerpf(0.22f, -0.04f, t);
            float e = 1.0f - (x / 0.18f) * (x / 0.18f) - ((y - 0.1f) / 0.155f) * ((y - 0.1f) / 0.155f);
            return Vec3{x, y, 0.12f * std::sqrt(std::max(e, 0.05f)) + 0.007f};
        };
        Vec3 a = onChest(t0), b = onChest(t1), d = b - a;
        solid->box(j[Chest] * translate((a + b) * 0.5f) * basisY(normalize(d)), {}, {0.016f, length(d) * 0.5f + 0.002f, 0.004f},
                   rgba(66, 50, 40), Material::Cloth, flash);
    }
    solid->box(j[Chest] * translate({0.04f, 0.115f, 0.124f}) * rotateZ(0.78f), {}, {0.018f, 0.014f, 0.004f}, rgba(120, 112, 100),
               Material::Metal, flash);  // hebilla
    if (powers_.attack) {  // hasta que la recoge, no lleva arma
        if (ws.style == WeaponStyle::Fists || ws.style == WeaponStyle::Claws) {  // los guanteletes van siempre puestos
            drawWeapon(*solid, j[WristR], ws, hero_.move(), hero_.moveU(), time_);
            drawWeapon(*solid, j[WristL], ws, hero_.move(), hero_.moveU(), time_);
        } else if (hero_.weaponInHand()) {
            drawWeapon(*solid, j[WristR], ws, hero_.move(), hero_.moveU(), time_);
        } else {  // envainada a la espalda, en diagonal: empuñadura sobre el hombro derecho
            Mat4 back = j[Chest] * translate({-0.1f, 0.25f, -0.14f}) * basisY(normalize(Vec3{0.45f, -1.0f, -0.05f})) *
                        rotateX(-kPi * 0.5f);
            drawWeapon(*solid, back, ws, Move::None, 0, time_);
        }
    }
    // Cinta de la espalda: segmentos alargados entre los puntos de la cadena, que se
    // estrechan hacia la punta.
    Vec3 rib[Character::kRibbonPoints];
    hero_.ribbon(alpha, rib);
    for (int i = 0; i + 1 < Character::kRibbonPoints; ++i) {
        Vec3 a = rib[i], b = rib[i + 1], d = b - a;
        float len = length(d);
        if (len < 1e-4f) continue;
        float t = float(i) / float(Character::kRibbonPoints - 1);
        float wdt = lerpf(0.055f, 0.04f, t);  // la cola de la bufanda
        solid->ellipsoid(translate((a + b) * 0.5f) * basisY(d), {}, {wdt, len * 0.62f, 0.014f}, heroLookColor(look, kScarfC),
                         Material::Cloth, flash);
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
    // Agujeros negros: una esfera de negrura con un borde violeta que gira y crece.
    for (const BlackHole& h : holes_) {
        float k = smoothstep01(h.age / 0.3f) * (1.0f - smoothstep01((h.age - h.life + 0.2f) / 0.2f));
        float r = (h.bomb ? 0.35f + 0.15f * std::sin(h.age * 30.0f) : 0.65f) * k;
        if (!solid) {
            dd.circleXZ(h.pos, r + 0.3f, 20, rgba(150, 80, 255));
            continue;
        }
        solid->ellipsoid(translate(h.pos), {}, {r, r, r}, rgba(6, 4, 10), Material::Cloth);
        glowRing(glow, h.pos, r * 1.05f, r * 1.45f, withAlpha(rgba(150, 80, 255), 0.55f * k));
        for (int i = 0; i < 3; ++i) {
            float a = h.age * (3.0f + float(i)) + float(i) * 2.1f;
            Vec3 p = h.pos + (view.right * std::cos(a) + view.up * std::sin(a)) * (r * 1.6f);
            glowStar(glow, view, p, 0.12f, a, withAlpha(rgba(220, 190, 255), 0.7f * k));
        }
    }
    // En modo sombra se ve dónde alumbra cada farola (ahí no se puede pasar fundido).
    if (shadowMode_)
        for (const Lamp& l : lamps_) {
            if (!l.on || !l.bulb) continue;
            Vec3 base{l.pos.x, groundHeight(l.pos + Vec3{0.6f, 0, 0}) + 0.03f, l.pos.z};
            float r = lampRadius(l) * 0.8f;
            if (!solid) dd.circleXZ(base, r, 28, rgba(255, 220, 160));
            else glowRing(glow, base, r * 0.96f, r, withAlpha(rgba(255, 220, 160), 0.4f));
        }
    // Portales: un aro vertical mitad luz, mitad sombra, que gira.
    for (const Portal& p : portals_) {
        Vec3 c = p.pos + Vec3{0, 1.3f, 0};
        const int n = 24;
        for (int i = 0; i < n; ++i) {
            float a = 2.0f * kPi * float(i) / float(n) + p.spin * 0.8f;
            Vec3 q = c + Vec3{std::cos(a) * 0.95f, std::sin(a) * 1.15f, 0};
            bool lightHalf = std::cos(a - p.spin * 0.8f) > 0;
            uint32_t col = lightHalf ? rgba(255, 228, 170) : rgba(150, 80, 255);
            if (!solid) dd.line(q, q + Vec3{0, 0.1f, 0}, col);
            else glowDot(glow, view, q, 0.16f, withAlpha(col, 0.85f));
        }
        if (solid) {
            glowDot(glow, view, c, 1.1f, withAlpha(shadowMode_ ? rgba(255, 228, 170) : rgba(150, 80, 255), 0.18f));
            solid->light(c, shadowMode_ ? Vec3{1.0f, 0.9f, 0.7f} : Vec3{0.6f, 0.35f, 1.0f}, 4.0f);
        }
    }
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
                       : p.kind == PickupKind::Xp    ? rgba(190, 240, 255)
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
            case PickupKind::Xp: {  // rombo pálido que gira, más grande cuanto más vale
                float r = 0.05f + 0.006f * float(p.value);
                Mat4 m = translate(c) * rotateY(time_ * 5.0f + p.seed) * rotateZ(kPi * 0.25f);
                solid->box(m, {}, {r, r, r * 0.4f}, col, Material::Glow);
                glowDot(glow, view, c, r * 3.0f, withAlpha(col, 0.35f));
                break;
            }
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
            case ProjectileKind::Burst: {  // sol pequeño que gira, con su luz
                solid->ellipsoid(translate(p.pos) * rotateY(time_ * 8.0f), {}, {0.26f, 0.26f, 0.26f}, rgba(255, 236, 190), Material::Glow);
                solid->light(p.pos, Vec3{1.0f, 0.75f, 0.4f} * 4.0f, 6.0f);
                glowDot(glow, view, p.pos, 0.8f, withAlpha(rgba(255, 190, 80), 0.6f));
                glowStar(glow, view, p.pos, 0.7f, time_ * 5.0f, withAlpha(rgba(255, 240, 200), 0.7f));
                break;
            }
            case ProjectileKind::Spikes: {  // un abanico de púas negras que salen del suelo
                Vec3 d = normalize(flat(p.vel)), sd{-d.z, 0, d.x};
                Vec3 ground = p.pos - Vec3{0, 0.9f, 0};
                for (int k = -2; k <= 2; ++k) {
                    float hgt = 0.6f + 0.35f * float(2 - std::abs(k)) + 0.1f * std::sin(time_ * 30.0f + float(k));
                    Vec3 base = ground + sd * (0.35f * float(k)) - d * (0.25f * float(std::abs(k)));
                    Vec3 tip = base + Vec3{0, hgt, 0} + d * 0.15f;
                    solid->ellipsoid(translate((base + tip) * 0.5f) * basisY(normalize(tip - base)), {}, {0.09f, hgt * 0.55f, 0.09f},
                                     rgba(26, 18, 40), Material::Stone);
                    solid->ellipsoid(translate(tip), {}, {0.04f, 0.04f, 0.04f}, rgba(170, 100, 255), Material::Glow);
                }
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

// Ecos: el cuerpo del protagonista hecho de luz blanca (un recuerdo de otros). Ilse: su
// propio cuerpo, oscuro, con dos puntos violetas por ojos.
void Game::drawGhosts(Renderer* solid, DebugDraw& lines, float alpha) const {
    for (const Ghost& g : ghosts_) {
        if (g.fade > 0.25f) continue;
        Mat4 j[JointCount];
        g.body.interpolatedJoints(alpha, j);
        if (!solid) {
            drawSkeleton(lines, kSkeleton, JointCount, j, DrawFlags{}, g.ilse ? rgba(150, 80, 255) : kGhostWhite);
            continue;
        }
        const BuiltModel& m = model(g.ilse ? ModelId::Ilse : ModelId::Hero);
        Mat4 skin[kMaxJoints];
        skinMatrices(m, j, skin);
        float appear = smoothstep01(g.age / 0.6f);
        if (g.ilse) {
            solid->skinned(m.mesh, skin, m.jointCount, 0.0f, rgba(60, 30, 90));
            for (float x : {0.04f, -0.04f})
                solid->ellipsoid(j[Head], {x, 0.128f, 0.105f}, {0.016f, 0.006f, 0.006f}, rgba(190, 130, 255), Material::Glow);
        } else {
            float glowF = 0.75f + 0.1f * std::sin(time_ * 3.0f + g.age);
            solid->skinned(m.mesh, skin, m.jointCount, glowF, kGhostWhite);
            solid->skinned(model(ModelId::HeroHead).mesh, skin, m.jointCount, glowF, 0);
            solid->light(translationOf(j[Chest]), Vec3{1.0f, 0.9f, 0.75f} * (1.2f * appear), 4.0f);
        }
    }
}
