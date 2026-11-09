#include "game.h"

#include "renderer.h"
#include "text.h"

#include <cstdio>

namespace {

// Destino de las cajas: el render con shaders o, en modo debug, el de líneas. Así el
// mismo código describe la escena para los dos modos.
struct BoxSink {
    DebugDraw& lines;
    Renderer* solid;  // nullptr = modo debug

    void box(const Mat4& t, Vec3 c, Vec3 h, uint32_t color, Material m, float flash = 0) const {
        if (solid)
            solid->box(t, c, h, color, m, flash);
        else
            lines.box(t, c, h, color);
    }
};

struct Palette {
    uint32_t skin, shirt, pants, shoes;
};

uint32_t partColor(int joint, const Palette& p) {
    switch (joint) {
        case Head: case Neck: case WristL: case WristR: return p.skin;
        case Pelvis: case HipL: case HipR: case KneeL: case KneeR: return p.pants;
        case AnkleL: case AnkleR: return p.shoes;
        default: return p.shirt;
    }
}

// Con shaders: cajas sólidas con colores de ropa. En debug: huesos, articulaciones y
// cajas en alambre de un solo color.
void drawActor(const BoxSink& out, const Mat4* joints, const DrawFlags& f, uint32_t debugColor,
               const Palette& palette, float flash) {
    for (int i = 0; i < JointCount; ++i) {
        const JointDef& j = kSkeleton[i];
        bool hasBox = j.boxHalf.x > 0 || j.boxHalf.y > 0 || j.boxHalf.z > 0;
        if (out.solid) {
            if (hasBox) out.box(joints[i], j.boxCenter, j.boxHalf, partColor(i, palette), Material::Cloth, flash);
            continue;
        }
        Vec3 p = translationOf(joints[i]);
        if (f.bones && j.parent >= 0) {
            // Convención clásica de herramientas 3D: izquierda azul, derecha roja.
            bool left = (i >= ShoulderL && i <= WristL) || (i >= HipL && i <= ToeL);
            bool right = (i >= ShoulderR && i <= WristR) || (i >= HipR && i <= ToeR);
            uint32_t col = left ? rgba(90, 160, 255) : right ? rgba(255, 110, 90) : rgba(255, 220, 80);
            out.lines.line(translationOf(joints[j.parent]), p, col);
        }
        if (f.joints) out.lines.point(p, rgba(255, 255, 255));
        if (f.body && hasBox) out.lines.box(joints[i], j.boxCenter, j.boxHalf, debugColor);
    }
}

void drawGrid(DebugDraw& dd) {
    const int half = 50;
    for (int i = -half; i <= half; ++i) {
        uint32_t col = (i % 10 == 0) ? rgba(90, 90, 100) : rgba(50, 52, 58);
        dd.line({float(i), 0, float(-half)}, {float(i), 0, float(half)}, col);
        dd.line({float(-half), 0, float(i)}, {float(half), 0, float(i)}, col);
    }
}

// Cóctel molotov: botella de cristal verde con un trapo ardiendo en el cuello. `o` es el
// centro de la botella en el espacio de `m`, con el cuello hacia +Z.
void drawBottle(const BoxSink& out, const Mat4& m, Vec3 o, float time, float seed) {
    out.box(m, o, {0.04f, 0.04f, 0.07f}, rgba(70, 150, 95), Material::Metal);                          // cuerpo
    out.box(m, o + Vec3{0, 0, 0.10f}, {0.016f, 0.016f, 0.035f}, rgba(70, 150, 95), Material::Metal);  // cuello
    out.box(m, o + Vec3{0, 0, 0.145f}, {0.022f, 0.022f, 0.02f}, rgba(225, 215, 185), Material::Cloth); // trapo
    Vec3 wick = transformPoint(m, o + Vec3{0, 0, 0.16f});
    if (!out.solid) {
        out.lines.point(wick, rgba(255, 140, 40));
        return;
    }
    float flicker = 0.75f + 0.25f * std::sin(time * 31.0f + seed) * std::sin(time * 13.0f + seed * 2.0f);
    out.solid->flame(wick - Vec3{0, 0.04f, 0}, 0.16f, 0.32f, seed, 1.0f);
    out.solid->light(wick, Vec3{1.0f, 0.5f, 0.15f} * (1.2f * flicker), 3.0f);
}

// Objeto en la mano derecha, en espacio local de la muñeca (Count = mano vacía).
void drawHeldItem(const BoxSink& out, const Character& hero, Item hand, float time) {
    const Mat4& w = hero.joints[WristR];
    switch (hand) {
        case Item::Rocks:
            out.box(w, kRockInHand, {0.05f, 0.05f, 0.05f}, rgba(170, 160, 150), Material::Stone);
            break;
        case Item::Molotov: drawBottle(out, w, kRockInHand, time, 3.0f); break;
        case Item::Sword:
            // Empuñadura y hoja salen perpendiculares al antebrazo (+Z local), como al cerrar el puño.
            out.box(w, {0, -0.08f, 0}, {0.015f, 0.015f, 0.06f}, rgba(140, 90, 50), Material::Cloth);       // mango
            out.box(w, {0, -0.08f, 0.07f}, {0.09f, 0.015f, 0.015f}, rgba(200, 180, 90), Material::Metal);  // guarda
            out.box(w, {0, -0.08f, 0.52f}, {0.025f, 0.006f, 0.44f}, rgba(220, 230, 240), Material::Metal); // hoja
            break;
        case Item::Pistol: {
            out.box(w, {0, -0.13f, 0.035f}, {0.018f, 0.09f, 0.025f}, rgba(160, 160, 170), Material::Metal);  // cañón
            out.box(w, {0, -0.07f, -0.02f}, {0.015f, 0.025f, 0.05f}, rgba(70, 70, 80), Material::Metal);     // culata
            // Mira láser desde el cañón hasta el punto apuntado (siempre es una línea).
            Vec3 muzzle = transformPoint(w, kPistolMuzzle);
            if (hero.aimHeld()) {
                out.lines.line(muzzle, hero.aimTarget(), rgba(230, 40, 40));
                out.lines.point(hero.aimTarget(), rgba(255, 60, 60));
            } else if (hero.aiming()) {
                out.lines.line(muzzle, hero.aimTarget(), rgba(140, 30, 30));
            }
            break;
        }
        default: break;
    }
}

// Barra de vida orientada hacia la cámara (billboard). Varias líneas paralelas dan grosor:
// en el perfil Core de OpenGL no se garantizan líneas de más de 1 px.
void drawHealthBar(DebugDraw& dd, Vec3 center, Vec3 right, float fraction) {
    const float half = 0.45f;
    Vec3 left = center - right * half;
    Vec3 split = left + right * (2.0f * half * fraction);
    Vec3 end = center + right * half;
    uint32_t fill = rgba(uint8_t(255 * (1 - fraction)), uint8_t(220 * fraction), 60);
    for (int i = 0; i < 5; ++i) {
        Vec3 o{0, 0.012f * float(i), 0};
        if (fraction > 0) dd.line(left + o, split + o, fill);
        if (fraction < 1) dd.line(split + o, end + o, rgba(70, 25, 25));
    }
}

// --- HUD: primitivas 2D en píxeles (origen arriba a la izquierda) ---

void hudLine(DebugDraw& hud, float x0, float y0, float x1, float y1, uint32_t c) {
    hud.line({x0, y0, 0}, {x1, y1, 0}, c);
}

void hudRect(DebugDraw& hud, float x0, float y0, float x1, float y1, uint32_t c) {
    hudLine(hud, x0, y0, x1, y0, c);
    hudLine(hud, x1, y0, x1, y1, c);
    hudLine(hud, x1, y1, x0, y1, c);
    hudLine(hud, x0, y1, x0, y0, c);
}

void hudNumber(DebugDraw& hud, float x, float y, float scale, int n, uint32_t c) {
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%d", n);
    drawText(hud, x, y, scale, buf, c);
}

// Iconos dibujados con líneas en una casilla de `s` píxeles con esquina en (x, y).
void hudIcon(DebugDraw& hud, Item item, float x, float y, float s, uint32_t c) {
    auto L = [&](float ax, float ay, float bx, float by) {
        hudLine(hud, x + ax * s, y + ay * s, x + bx * s, y + by * s, c);
    };
    switch (item) {
        case Item::Fists:
            L(0.30f, 0.40f, 0.70f, 0.40f); L(0.70f, 0.40f, 0.70f, 0.72f);
            L(0.70f, 0.72f, 0.30f, 0.72f); L(0.30f, 0.72f, 0.30f, 0.40f);
            L(0.40f, 0.40f, 0.40f, 0.52f); L(0.50f, 0.40f, 0.50f, 0.52f);
            L(0.60f, 0.40f, 0.60f, 0.52f); L(0.30f, 0.58f, 0.50f, 0.62f);
            break;
        case Item::Sword:
            L(0.38f, 0.62f, 0.78f, 0.22f); L(0.42f, 0.66f, 0.82f, 0.26f);  // hoja
            L(0.78f, 0.22f, 0.82f, 0.26f);
            L(0.26f, 0.56f, 0.44f, 0.74f);                                 // guarda
            L(0.36f, 0.64f, 0.22f, 0.78f);                                 // mango
            break;
        case Item::Pistol:
            L(0.20f, 0.34f, 0.80f, 0.34f); L(0.80f, 0.34f, 0.80f, 0.46f);  // cañón
            L(0.80f, 0.46f, 0.20f, 0.46f); L(0.20f, 0.46f, 0.20f, 0.34f);
            L(0.24f, 0.46f, 0.20f, 0.74f); L(0.20f, 0.74f, 0.34f, 0.74f);  // culata
            L(0.34f, 0.74f, 0.38f, 0.46f);
            L(0.44f, 0.46f, 0.46f, 0.56f); L(0.46f, 0.56f, 0.38f, 0.56f);  // gatillo
            break;
        case Item::Molotov:
            L(0.38f, 0.44f, 0.62f, 0.44f); L(0.62f, 0.44f, 0.62f, 0.80f);  // botella
            L(0.62f, 0.80f, 0.38f, 0.80f); L(0.38f, 0.80f, 0.38f, 0.44f);
            L(0.45f, 0.44f, 0.45f, 0.30f); L(0.55f, 0.44f, 0.55f, 0.30f);  // cuello
            L(0.45f, 0.30f, 0.55f, 0.30f);
            L(0.50f, 0.28f, 0.42f, 0.18f); L(0.42f, 0.18f, 0.50f, 0.20f);  // llama
            L(0.50f, 0.20f, 0.55f, 0.08f); L(0.55f, 0.08f, 0.58f, 0.22f);
            L(0.58f, 0.22f, 0.50f, 0.28f);
            break;
        case Item::Rocks: {
            float px = 0.5f + 0.2f, py = 0.52f;
            for (int i = 1; i <= 8; ++i) {
                float a = 2.0f * kPi * float(i) / 8.0f;
                float r = (i % 2) ? 0.2f : 0.17f;
                float nx = 0.5f + r * std::cos(a), ny = 0.52f + r * 0.8f * std::sin(a);
                L(px, py, nx, ny);
                px = nx;
                py = ny;
            }
            break;
        }
        default: break;
    }
}

}  // namespace

void Game::draw(DebugDraw& dd, Renderer* solid, const DrawFlags& f, Vec3 cameraRight) const {
    BoxSink out{dd, solid};
    Mat4 id = Mat4::identity();

    if (solid) {
        // Suelo: una losa enorme; la cuadrícula la dibuja el shader.
        solid->box(id, {0, -0.5f, 0}, {200, 0.5f, 200}, rgba(104, 128, 92), Material::Ground);
        for (const AABB& b : world_.solids)
            solid->box(id, b.center(), b.halfExtents(), rgba(196, 170, 136), Material::Stone);
    } else {
        if (f.grid) drawGrid(dd);
        Vec3 o{0, 0.001f, 0};
        dd.line(o, {1, 0.001f, 0}, rgba(255, 60, 60));
        dd.line(o, {0, 1, 0}, rgba(60, 255, 60));
        dd.line(o, {0, 0.001f, 1}, rgba(60, 120, 255));
        // Plataformas: caja + aspa en la cara superior para leer bien dónde se pisa.
        for (const AABB& b : world_.solids) {
            dd.box(id, b.center(), b.halfExtents(), rgba(150, 120, 80));
            float y = b.max.y;
            dd.line({b.min.x, y, b.min.z}, {b.max.x, y, b.max.z}, rgba(110, 90, 60));
            dd.line({b.min.x, y, b.max.z}, {b.max.x, y, b.min.z}, rgba(110, 90, 60));
        }
    }

    // Jugador.
    const Palette heroPalette{rgba(232, 190, 150), rgba(46, 160, 150), rgba(46, 60, 100), rgba(40, 34, 32)};
    drawActor(out, hero_.joints, f, rgba(80, 200, 190), heroPalette, 0);
    drawHeldItem(out, hero_, handItem(), time_);
    if (!solid) {
        Vec3 base = hero_.body.pos + Vec3{0, 0.005f, 0};
        dd.circleXZ(base, 0.35f, 24, rgba(120, 120, 120));
        dd.line(base, base + hero_.forward() * 0.6f, rgba(255, 255, 0));
    }

    // Enemigos: ropa roja, escudo metálico y barra de vida. Destellan al recibir un golpe.
    const Palette enemyPalette{rgba(200, 160, 130), rgba(180, 52, 48), rgba(64, 44, 44), rgba(36, 30, 30)};
    const Palette deadPalette{rgba(120, 100, 90), rgba(90, 50, 48), rgba(50, 40, 40), rgba(30, 26, 26)};
    for (const Enemy& e : enemies_) {
        float flash = e.alive() ? clampf((e.hitTimer - 0.25f) / 0.15f, 0, 1) * 0.8f : 0;
        uint32_t debugCol = !e.alive() ? rgba(90, 60, 60) : e.hitTimer > 0.25f ? rgba(255, 255, 255) : rgba(220, 90, 90);
        drawActor(out, e.joints, f, debugCol, e.alive() ? enemyPalette : deadPalette, flash);
        uint32_t shieldCol = e.blockTimer > 0 ? rgba(255, 245, 190) : rgba(110, 140, 220);
        out.box(e.joints[Chest], kShieldCenter, kShieldHalf, shieldCol, Material::Metal, e.blockTimer > 0 ? 0.5f : 0);
        if (e.alive()) {
            Vec3 head = translationOf(e.joints[Head]);
            drawHealthBar(dd, head + Vec3{0, 0.45f, 0}, cameraRight, e.health / e.maxHealth);
        }
    }

    // Proyectiles y partículas (las balas y chispas son líneas en los dos modos).
    for (const Projectile& p : projectiles_) {
        if (p.kind == ProjectileKind::Bullet)
            dd.line(p.pos - p.vel * 0.02f, p.pos, rgba(255, 240, 150));
        else if (p.kind == ProjectileKind::Molotov)
            drawBottle(out, translate(p.pos) * rotateX(p.spin) * rotateY(p.spin * 0.6f), {}, time_, 5.0f);
        else
            out.box(id, p.pos, {0.06f, 0.06f, 0.06f}, p.armed ? rgba(230, 220, 200) : rgba(150, 140, 130),
                    Material::Stone);
    }
    for (const Particle& p : particles_) {
        dd.point(p.pos, p.color);
        dd.line(p.pos, p.pos - p.vel * 0.03f, p.color);
    }
    drawFire(dd, solid, cameraRight);
}

// Fuego de los cócteles. En modo debug: círculo del área y una línea por lengua.
void Game::drawFire(DebugDraw& dd, Renderer* solid, Vec3 cameraRight) const {
    const Vec3 fireColor{1.0f, 0.45f, 0.12f};
    for (const Fire& fire : fires_) {
        float s = fire.strength(), r = fire.radius();
        if (!solid) {
            dd.circleXZ(fire.pos + Vec3{0, 0.03f, 0}, r, 32, rgba(255, 140, 40));
            for (const FireTongue& t : fire.tongues)
                if (t.dist <= r) dd.line(t.pos, t.pos + Vec3{0, 0.8f * s, 0}, rgba(255, 170, 60));
            continue;
        }
        if (s <= 0) continue;
        // Las lenguas del borde son más bajas: el charco arde más fuerte en el centro.
        for (const FireTongue& t : fire.tongues) {
            if (t.dist > r) continue;
            float edge = 1.0f - 0.45f * t.dist / fire.maxRadius;
            solid->flame(t.pos, 0.55f + 0.5f * t.size, (0.7f + 1.0f * t.size) * edge * s, t.seed, s);
        }
        float flicker = 0.8f + 0.2f * std::sin(time_ * 23.0f + fire.seed) * std::sin(time_ * 7.3f + fire.seed * 3.0f);
        solid->light(fire.pos + Vec3{0, 0.8f, 0}, fireColor * (4.0f * s * flicker), 7.0f);
    }

    for (const Blast& b : blasts_) {
        float grow = smoothstep01(b.age / 0.15f), fade = 1.0f - b.age / 0.6f;
        if (!solid) {
            dd.circleXZ(b.pos, 3.0f * grow, 24, rgba(255, 230, 120));
            continue;
        }
        solid->flame(b.pos - Vec3{0, 0.4f, 0}, 3.0f * grow, 3.4f * grow, b.seed, 1.25f * fade);
        solid->light(b.pos + Vec3{0, 1.0f, 0}, fireColor * (7.0f * fade), 10.0f);
    }
    if (!solid) return;

    for (const ScorchMark& m : scorches_)
        solid->scorch(m.pos, m.radius * smoothstep01(m.age / 0.4f), 1.0f - m.age / 40.0f);
    for (const SmokePuff& p : smoke_) {
        float opacity = 0.45f * std::min(1.0f, p.age / 0.4f) * (1.0f - p.age / p.life);
        solid->smoke(p.pos, 0.8f + p.age * 0.9f, p.seed, opacity);
    }

    // Enemigos ardiendo: llamas sobre el cuerpo, adelantadas hacia la cámara para que el
    // propio cuerpo no las tape, y una luz que los ilumina desde dentro.
    Vec3 toCamera = cross(cameraRight, {0, 1, 0});
    const int burningParts[] = {Pelvis, Chest, Head};
    for (size_t i = 0; i < enemies_.size(); ++i) {
        const Enemy& e = enemies_[i];
        if (e.burnTimer <= 0 || !e.alive()) continue;
        float k = std::min(1.0f, e.burnTimer);
        for (int part : burningParts) {
            Vec3 base = translationOf(e.joints[part]) - Vec3{0, 0.25f, 0} + toCamera * 0.22f;
            solid->flame(base, 0.55f, 0.9f * k, float(part) * 7.0f + float(i) * 3.0f, k);
        }
        solid->light(translationOf(e.joints[Chest]), fireColor * (2.0f * k), 4.0f);
    }
}

void Game::drawHud(DebugDraw& hud, int width, int height) const {
    // Inventario: una casilla por objeto, centrado abajo. 1-4 seleccionan.
    const float size = 56, gap = 10;
    const int slots = int(Item::Count);
    float x0 = (float(width) - (slots * size + (slots - 1) * gap)) * 0.5f;
    float y0 = float(height) - size - 24;
    for (int i = 0; i < slots; ++i) {
        float x = x0 + float(i) * (size + gap);
        bool selected = int(hero_.item) == i;
        int ammo = Item(i) == Item::Rocks ? rocks_ : Item(i) == Item::Molotov ? molotovs_ : -1;
        bool empty = ammo == 0;
        uint32_t frame = selected ? rgba(255, 220, 80) : rgba(110, 110, 120);
        hudRect(hud, x, y0, x + size, y0 + size, frame);
        if (selected) hudRect(hud, x + 3, y0 + 3, x + size - 3, y0 + size - 3, frame);
        hudNumber(hud, x + 5, y0 + 5, 1.5f, i + 1, rgba(150, 150, 160));
        hudIcon(hud, Item(i), x, y0, size, empty ? rgba(80, 80, 85) : rgba(235, 235, 240));
        if (ammo >= 0) {
            char buf[16];
            std::snprintf(buf, sizeof(buf), "%d", ammo);
            drawText(hud, x + size - 5 - textWidth(buf, 2), y0 + size - 17, 2, buf, rgba(255, 220, 80));
        }
    }

    // Derribos (arriba a la izquierda): calavera esquemática + contador.
    uint32_t red = rgba(230, 90, 90);
    hudRect(hud, 20, 20, 44, 40, red);
    hudLine(hud, 26, 40, 26, 46, red);
    hudLine(hud, 32, 40, 32, 46, red);
    hudLine(hud, 38, 40, 38, 46, red);
    hudRect(hud, 25, 26, 30, 31, red);
    hudRect(hud, 34, 26, 39, 31, red);
    hudNumber(hud, 54, 22, 4, kills_, red);

    // Mira: cruz con hueco central; roja si hay un enemigo debajo.
    if (hero_.aimHeld()) {
        float cx = float(width) * 0.5f, cy = float(height) * 0.5f;
        uint32_t c = aimOnEnemy_ ? rgba(255, 70, 70) : rgba(240, 240, 240);
        hudLine(hud, cx - 14, cy, cx - 4, cy, c);
        hudLine(hud, cx + 4, cy, cx + 14, cy, c);
        hudLine(hud, cx, cy - 14, cx, cy - 4, c);
        hudLine(hud, cx, cy + 4, cx, cy + 14, c);
        hud.point({cx, cy, 0}, c);
    }
}
