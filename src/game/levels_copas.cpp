// Las Copas: una aldea en lo alto de unos árboles gigantes, al anochecer. Cuatro rincones
// pequeños, cada uno en su árbol y unidos por pasarelas colgantes que se comban al cruzarlas;
// abajo, muy abajo, los troncos se pierden en la niebla.
//   - El Tronco Mayor: la plaza de tablas alrededor del árbol más grande, con la posada, dos
//     casitas, un puesto de té y la escalera de caracol que sube por el tronco.
//   - Los Talleres: un árbol más bajo, con el taller de la polea y su grúa sobre el vacío.
//   - El Jardín colgante: macetas, un cerezo en su jardinera y una casita.
//   - El Mirador: una plataforma diminuta en lo alto, al final de la pasarela más larga, con
//     el catalejo y la barandilla baja.
// Alrededor, otros árboles con ventanas encendidas: el telón de fondo. Sin suelo: quien se cae
// vuelve al último punto seguro.

#include "game/game.h"
#include "game/map_tools.h"
#include "game/props.h"

#include <algorithm>
#include <cmath>

using namespace maptools;

namespace {

const uint32_t kPlank = rgba(150, 110, 72), kPlankDark = rgba(98, 70, 46), kRope = rgba(176, 150, 106);
const uint32_t kWarm = rgba(255, 206, 120), kMist = rgba(150, 156, 176);

// Caja orientada de `a` a `b` (vigas, cuerdas): el eje Y de la caja va a lo largo.
Mat4 along(Vec3 a, Vec3 b) {
    Vec3 d = b - a, y = normalize(d);
    Vec3 ref = std::fabs(y.y) > 0.95f ? Vec3{1, 0, 0} : Vec3{0, 1, 0};
    Vec3 x = normalize(cross(ref, y)), z = cross(x, y);
    Mat4 m = Mat4::identity();
    m.m[0] = x.x, m.m[1] = x.y, m.m[2] = x.z, m.m[4] = y.x, m.m[5] = y.y, m.m[6] = y.z, m.m[8] = z.x, m.m[9] = z.y, m.m[10] = z.z;
    m.m[12] = (a.x + b.x) / 2, m.m[13] = (a.y + b.y) / 2, m.m[14] = (a.z + b.z) / 2;
    return m;
}

}  // namespace

void Game::buildCopas() {
    levelName_ = "Las Copas";
    world_.ground = false;
    night_ = 0.8f;  // anochecer: las ventanas y los farolillos ya encendidos
    fogScale_ = 1.3f;
    viewDistance_ = 260;
    const Mat4 id = Mat4::identity();
    Rng rng{4242};

    auto beam = [&](Vec3 a, Vec3 b, float w, uint32_t col) { addDecor(along(a, b), {}, {w, length(b - a) * 0.5f, w}, col, Material::Cloth, false); };
    // Árbol gigante: tronco que baja a la niebla (su copa queda `crown` metros por encima). El
    // tronco gira solo de cuarto en cuarto: girado de otro modo, su caja de choque (alineada con
    // los ejes) crecería hasta tapar los peldaños de la escalera en las diagonales.
    auto giant = [&](Vec3 base, float top, float scl, const char* crown, float crownScl) {
        addProp("tronco_gigante", {base.x, top - 150.0f * scl, base.z}, float(rng.pick(4)) * kPi * 0.5f, scl, false);
        if (crown) addProp(crown, {base.x, top - 1.0f, base.z}, rng.range(0, 6.28f), crownScl, false);
    };
    // Plataforma de tablas (arriba en y = top), con vigas que la apoyan en el tronco.
    auto deck = [&](Vec3 c, float hx, float hz, float top, Vec3 trunk) {
        addBlock({c.x, top - 0.2f, c.z}, {hx, 0.2f, hz}, kPlank, Material::Cloth);
        for (float x = -hx + 0.45f; x < hx; x += 0.9f)  // juntas de los tablones
            addDecor(id, {c.x + x, top + 0.004f, c.z}, {0.018f, 0.006f, hz * 0.99f}, kPlankDark, Material::Cloth, false);
        addDecor(id, {c.x, top - 0.2f, c.z - hz}, {hx + 0.06f, 0.22f, 0.08f}, kPlankDark, Material::Cloth, false);  // cantos
        addDecor(id, {c.x, top - 0.2f, c.z + hz}, {hx + 0.06f, 0.22f, 0.08f}, kPlankDark, Material::Cloth, false);
        addDecor(id, {c.x - hx, top - 0.2f, c.z}, {0.08f, 0.22f, hz + 0.06f}, kPlankDark, Material::Cloth, false);
        addDecor(id, {c.x + hx, top - 0.2f, c.z}, {0.08f, 0.22f, hz + 0.06f}, kPlankDark, Material::Cloth, false);
        for (float sx : {-1.0f, 1.0f})  // jabalcones de las esquinas al tronco
            for (float sz : {-1.0f, 1.0f}) {
                Vec3 corner{c.x + sx * (hx - 0.4f), top - 0.4f, c.z + sz * (hz - 0.4f)};
                Vec3 into = flat(trunk - corner);
                beam(corner, Vec3{trunk.x, top - 4.5f - 0.15f * length(into), trunk.z} - normalize(into) * 2.6f, 0.13f, kPlankDark);
            }
    };
    // Barandilla de `a` a `b` (a lo largo de X o de Z, en y = top): postes, pasamanos y una caja
    // que no deja caerse andando (saltando, sí).
    auto rail = [&](Vec3 a, Vec3 b, float top, float h = 0.95f) {
        Vec3 d = flat(b - a);
        float len = length(d);
        if (len < 0.3f) return;
        Vec3 dir = d * (1.0f / len);
        for (float t = 0; t <= len + 0.01f; t += 1.6f)
            addDecor(id, a + dir * std::min(t, len) + Vec3{0, top + h * 0.5f, 0}, {0.07f, h * 0.5f, 0.07f}, kPlankDark, Material::Cloth, false);
        Vec3 c = (a + b) * 0.5f;
        bool alongX = std::fabs(dir.x) > 0.5f;
        Vec3 half = alongX ? Vec3{len * 0.5f, 0, 0.05f} : Vec3{0.05f, 0, len * 0.5f};
        addDecor(id, {c.x, top + h, c.z}, half + Vec3{0, 0.05f, 0}, kPlank, Material::Cloth, false);
        addDecor(id, {c.x, top + h * 0.5f, c.z}, half + Vec3{0, 0.03f, 0}, kPlank, Material::Cloth, false);
        blocks_.push_back({boxFromCenter({c.x, top + h * 0.5f, c.z}, half + Vec3{0, h * 0.5f, 0.05f}), 0, Material::Cloth});  // invisible
    };
    // Las cuatro barandillas de una plataforma, con huecos (centro y medio ancho) por lado.
    struct Gap {
        int side;  // 0 = -z, 1 = +x, 2 = +z, 3 = -x
        float at, half;
    };
    auto railings = [&](Vec3 c, float hx, float hz, float top, std::initializer_list<Gap> gaps, float h = 0.95f) {
        for (int side = 0; side < 4; ++side) {
            bool alongX = side % 2 == 0;
            float fixed = side == 0 ? c.z - hz : side == 1 ? c.x + hx : side == 2 ? c.z + hz : c.x - hx;
            float lo = alongX ? c.x - hx : c.z - hz, hi = alongX ? c.x + hx : c.z + hz;
            std::vector<std::pair<float, float>> open;
            for (const Gap& g : gaps)
                if (g.side == side) open.push_back({g.at - g.half, g.at + g.half});
            std::sort(open.begin(), open.end());
            float from = lo;
            auto seg = [&](float s0, float s1) {
                Vec3 a = alongX ? Vec3{s0, 0, fixed} : Vec3{fixed, 0, s0}, b = alongX ? Vec3{s1, 0, fixed} : Vec3{fixed, 0, s1};
                rail(a, b, top, h);
            };
            for (auto [o0, o1] : open) {
                seg(from, o0);
                from = o1;
            }
            seg(from, hi);
        }
    };
    // Pasarela colgante de `a` a `b` (a lo largo de X o de Z): tablas sueltas que se comban,
    // con huecos entre ellas por los que se ve el fondo, y cuerdas a la altura de la mano.
    auto bridge = [&](Vec3 a, Vec3 b, float sag, int missing = -1) {
        Vec3 d = flat(b - a);
        float len = length(d);
        Vec3 dir = d * (1.0f / len), side{-dir.z, 0, dir.x};
        bool alongX = std::fabs(dir.x) > 0.5f;
        auto yAt = [&](float t) { return a.y + (b.y - a.y) * t - sag * 4 * t * (1 - t); };
        int n = int(len / 0.55f);
        float step = len / float(n);
        for (int i = 0; i < n; ++i) {
            float t = (float(i) + 0.5f) / float(n);
            Vec3 c = a + dir * (step * (float(i) + 0.5f));
            float y = yAt(t);
            Vec3 h = alongX ? Vec3{step * 0.5f, 0.06f, 0.8f} : Vec3{0.8f, 0.06f, step * 0.5f};
            if (i != missing) {
                blocks_.push_back({boxFromCenter({c.x, y - 0.06f, c.z}, h), 0, Material::Cloth});  // la que choca (invisible)
                Vec3 v = alongX ? Vec3{step * 0.5f - 0.04f, 0.05f, 0.78f - 0.06f * float(i % 3)} : Vec3{0.78f - 0.06f * float(i % 3), 0.05f, step * 0.5f - 0.04f};
                addDecor(translate({c.x, y - 0.05f, c.z}) * rotateY(0.04f * float(i % 5 - 2)), {}, v, i % 4 ? kPlank : kPlankDark, Material::Cloth, false);
            }
            for (float s : {-0.9f, 0.9f}) {  // barandilla: la cuerda y una caja que no deja caerse andando
                Vec3 r = c + side * s;
                Vec3 rh = alongX ? Vec3{step * 0.5f, 0.5f, 0.04f} : Vec3{0.04f, 0.5f, step * 0.5f};
                blocks_.push_back({boxFromCenter({r.x, y + 0.5f, r.z}, rh), 0, Material::Cloth});
            }
        }
        for (float s : {-0.9f, 0.9f}) {  // cuerdas por tramos y postes cada pocos metros
            for (int k = 0; k < 12; ++k) {
                float t0 = float(k) / 12, t1 = float(k + 1) / 12;
                Vec3 p0 = a + d * t0 + side * s, p1 = a + d * t1 + side * s;
                p0.y = yAt(t0) + 1.0f, p1.y = yAt(t1) + 1.0f;
                beam(p0, p1, 0.025f, kRope);
                beam(p0 - Vec3{0, 0.95f, 0}, p1 - Vec3{0, 0.95f, 0}, 0.03f, kRope);
            }
            for (float t = 0; t <= 1.001f; t += 1.0f / std::max(2.0f, std::round(len / 2.2f))) {
                Vec3 p = a + d * t + side * s;
                p.y = yAt(t);
                addDecor(id, p + Vec3{0, 0.5f, 0}, {0.04f, 0.52f, 0.04f}, kPlankDark, Material::Cloth, false);
            }
        }
    };
    // Guirnalda de luces de `a` a `b`: bombillas cálidas colgando de un cable combado.
    auto garland = [&](Vec3 a, Vec3 b, float sag) {
        int n = int(length(b - a) / 0.9f);
        for (int i = 0; i <= n; ++i) {
            float t = float(i) / float(std::max(n, 1));
            Vec3 p = a + (b - a) * t - Vec3{0, sag * 4 * t * (1 - t), 0};
            if (i > 0) {
                float t0 = float(i - 1) / float(n);
                beam(a + (b - a) * t0 - Vec3{0, sag * 4 * t0 * (1 - t0), 0}, p, 0.012f, rgba(40, 34, 30));
            }
            addDecor(id, p - Vec3{0, 0.12f, 0}, {0.07f, 0.09f, 0.07f}, i % 3 == 1 ? rgba(255, 170, 110) : kWarm, Material::Glow, true);
        }
    };

    // --- El Tronco Mayor ------------------------------------------------------------------
    const Vec3 A{0, 0, 0};
    giant(A, 11, 1.0f, "copa_gigante", 1.25f);
    deck(A, 10, 10, 0, A);
    railings(A, 10, 10, 0, {{1, -3, 1.0f}, {3, 6, 1.0f}});
    addProp("posada_copa", {-5, 0, -6.6f}, 0);  // de cara al tronco
    addProp("casa_copa_miel", {6.4f, 0, 6.4f}, -kPi * 0.75f);
    addProp("casa_copa_salvia", {6.2f, 0, -6.6f}, -kPi * 0.25f);
    addProp("puesto_comida", {-6.5f, 0, 8.2f}, kPi);
    for (Vec3 p : {Vec3{-9.2f, 0, 9.2f}, Vec3{9.2f, 0, -1.6f}, Vec3{-9.2f, 0, -9.2f}, Vec3{9.2f, 0, 9.2f}, Vec3{-9.2f, 0, 3}})
        addProp("farolillo_copa", p, std::atan2(-p.x, -p.z));
    for (Vec3 p : {Vec3{-9.6f, 0, -3}, Vec3{-1.2f, 0, -9.3f}, Vec3{2.8f, 0, 9.2f}, Vec3{9.2f, 0, 3.2f}})
        addProp(int(p.x + p.z) % 2 ? "maceta_roja" : "maceta_lila", p, rng.range(0, 6.28f));
    addProp("barril", {-0.4f, 0, -8.8f});
    addProp("barril", {0.5f, 0, -9.2f}, 0.7f);
    addProp("banca_parque", {-3.2f, 0, 3.6f}, -kPi * 0.25f);
    addProp("banca_parque", {4.2f, 0, -2.4f}, kPi * 0.6f);
    // El punto focal: el tronco, con la escalera de caracol hasta un balcón a 6 m y guirnaldas
    // de luces de las ramas a las esquinas.
    const float kUpper = 6.0f;
    const int kSteps = 22;
    for (int i = 0; i < kSteps; ++i) {
        float a = (50.0f + float(i) * 260.0f / float(kSteps - 1)) * kPi / 180.0f;
        float h = kUpper * float(i + 1) / float(kSteps);
        Vec3 c{std::sin(a) * 3.7f, 0, std::cos(a) * 3.7f};
        addBlock({c.x, h - 0.12f, c.z}, {0.62f, 0.12f, 0.62f}, i % 2 ? kPlank : kPlankDark, Material::Cloth);
        if (h > 0.3f) blocks_.push_back({boxFromCenter({c.x, (h - 0.24f) * 0.5f, c.z}, {0.5f, (h - 0.24f) * 0.5f, 0.5f}), 0, Material::Cloth});
        beam(c * 1.12f + Vec3{0, h - 0.2f, 0}, c * 0.72f + Vec3{0, h - 1.2f, 0}, 0.06f, kPlankDark);  // ménsula al tronco
        if (i % 3 == 0) addDecor(id, c * 1.2f + Vec3{0, h + 0.5f, 0}, {0.04f, 0.5f, 0.04f}, kPlankDark, Material::Cloth, false);
    }
    addBlock({0, kUpper - 0.2f, 4.0f}, {3.4f, 0.2f, 1.6f}, kPlank, Material::Cloth);  // el balcón del tronco
    rail({3.4f, 0, 2.4f}, {3.4f, 0, 5.6f}, kUpper);
    rail({-3.4f, 0, 5.6f}, {-0.9f, 0, 5.6f}, kUpper);
    rail({0.9f, 0, 5.6f}, {3.4f, 0, 5.6f}, kUpper);
    addProp("farolillo_copa", {2.9f, kUpper, 5.1f}, kPi, 1.0f, false);
    for (Vec3 corner : {Vec3{-9.6f, 3.0f, -9.6f}, Vec3{9.6f, 3.0f, 9.6f}, Vec3{-9.6f, 3.0f, 9.6f}, Vec3{9.6f, 3.0f, -9.6f}}) {
        addDecor(id, corner - Vec3{0, 1.5f, 0}, {0.06f, 1.5f, 0.06f}, kPlankDark, Material::Cloth, false);
        garland(normalize(flat(corner)) * 3.0f + Vec3{0, 8.5f, 0}, corner, 1.2f);
    }

    // --- Los Talleres (más abajo, al este) --------------------------------------------------
    const Vec3 B{30, 0, -3}, trunkB{31.5f, 0, -0.5f};
    const float kB = -1.5f;
    giant(trunkB, 9, 0.8f, "copa_gigante_b", 1.0f);
    deck(B, 6, 6, kB, trunkB);
    railings(B, 6, 6, kB, {{3, -3, 1.0f}});
    addProp("taller_copa", {27.6f, kB, -4.6f}, kPi * 0.5f, 1.0f, false);  // la grúa asoma al vacío por el sur
    addProp("casa_copa_rosa", {32.6f, kB, 1.2f}, -kPi * 0.5f, 1.0f, false);
    addProp("farolillo_copa", {35.4f, kB, -8.4f}, -kPi * 0.75f, 1.0f, false);
    addProp("barril", {34.6f, kB, -6.2f}, 0, 1.0f, false);
    addProp("barril", {35.3f, kB, -5.4f}, 0.5f, 1.0f, false);
    // La cuerda de la grúa sigue bajando hasta perderse, con otra cesta a medio camino.
    Vec3 hook{27.6f + 1.6f, kB, -4.6f - 5.9f};
    beam(hook + Vec3{0, 0.9f, 0}, hook - Vec3{0, 60, 0}, 0.02f, kRope);
    addDecor(id, hook - Vec3{0, 22, 0}, {0.5f, 0.3f, 0.5f}, rgba(160, 122, 64), Material::Cloth, false);

    // --- El Jardín colgante (más arriba, al oeste) -----------------------------------------
    const Vec3 D{-27, 0, 6}, trunkD{-29.5f, 0, 3.5f};
    const float kD = 1.5f;
    giant(trunkD, 13, 0.8f, "copa_gigante_b", 1.1f);
    deck(D, 6, 5, kD, trunkD);
    railings(D, 6, 5, kD, {{1, 6, 1.0f}});
    addProp("casa_copa_salvia", {-30.2f, kD, 8.0f}, kPi * 0.6f, 1.0f, false);
    addProp("cerezo", {-23.6f, kD, 9.0f}, 0.4f, 0.8f, false);  // en su jardinera
    addDecor(id, {-23.6f, kD + 0.3f, 9.0f}, {1.2f, 0.3f, 1.2f}, kPlankDark, Material::Cloth, false);
    blocks_.push_back({boxFromCenter({-23.6f, kD + 0.3f, 9.0f}, {1.2f, 0.3f, 1.2f}), 0, Material::Cloth});
    for (int i = 0; i < 9; ++i) {
        Vec3 p{-32.4f + float(i % 5) * 1.3f, kD, i < 5 ? 1.5f : 2.6f};
        if (length(flat(p - trunkD)) < 3.2f) continue;
        addProp(i % 3 ? "maceta_roja" : "maceta_lila", p, rng.range(0, 6.28f), rng.range(0.9f, 1.2f), false);
    }
    for (Vec3 p : {Vec3{-24, 0, 3.0f}, Vec3{-22.2f, 0, 2.0f}, Vec3{-25, 0, 10.4f}})
        addProp(p.x > -23 ? "arbusto_flores" : "maceta_lila", p + Vec3{0, kD, 0}, rng.range(0, 6.28f), 0.8f, false);
    addProp("banca_parque", {-26.5f, kD, 3.8f}, kPi * 0.5f, 1.0f, false);
    addProp("farolillo_copa", {-21.6f, kD, 1.6f}, -kPi * 0.25f, 1.0f, false);
    addProp("farolillo_copa", {-32.4f, kD, 10.4f}, kPi * 0.75f, 1.0f, false);

    // --- El Mirador (arriba del todo, al norte) --------------------------------------------
    const Vec3 C{0, 0, 33.5f}, trunkC{0, 0, 37.6f};
    const float kC = 8.0f;
    giant(trunkC, 17, 0.6f, "copa_gigante_b", 0.75f);
    deck(C, 3.2f, 3.2f, kC, trunkC);
    railings(C, 3.2f, 3.2f, kC, {{0, 0, 0.95f}}, 0.7f);  // la barandilla baja: el vértigo
    addProp("catalejo", {2.0f, kC, 32.0f}, kPi * 0.6f, 1.0f, false);
    addProp("farolillo_copa", {-2.7f, kC, 31.0f}, kPi * 0.5f, 1.0f, false);
    addProp("banca_parque", {-1.6f, kC, 34.2f}, kPi * 0.5f, 1.0f, false);

    // --- Pasarelas -----------------------------------------------------------------------
    bridge({10, 0, -3}, {24, kB, -3}, 0.8f);
    bridge({-10, 0, 6}, {-21, kD, 6}, 0.6f);
    bridge({0, kUpper, 5.6f}, {0, kC, 30.3f}, 1.5f, 27);  // la más larga, con una tabla que falta

    // --- El telón de fondo: otros árboles de la aldea, con casas encendidas ---------------
    for (int i = 0; i < 14; ++i) {
        float a = float(i) / 14 * 2 * kPi + rng.range(-0.15f, 0.15f), r = rng.range(70, 125);
        Vec3 p{std::cos(a) * r, 0, std::sin(a) * r + 10};
        float top = rng.range(4, 26), scl = rng.range(0.7f, 1.2f);
        giant(p, top, scl, rng.next() < 0.5f ? "copa_gigante" : "copa_gigante_b", scl * rng.range(1.0f, 1.3f));
        if (rng.next() < 0.75f) {  // una plataforma con su casita y su luz
            float y = top - rng.range(6, 12);
            Vec3 dir = normalize(Vec3{-p.x, 0, -p.z}), side{-dir.z, 0, dir.x};
            Vec3 c = p + dir * 4.5f;
            addDecor(id, {c.x, y - 0.2f, c.z}, {3.6f, 0.2f, 3.6f}, kPlank, Material::Cloth, false);
            const char* houses[] = {"casa_copa_miel", "casa_copa_rosa", "casa_copa_salvia"};
            addProp(houses[rng.pick(3)], {c.x, y, c.z}, std::atan2(dir.x, dir.z), 1.0f, false);
            if (rng.next() < 0.5f) {  // y una pasarela a ninguna parte que se pierde entre los troncos
                Vec3 e = c + side * 3.6f;
                beam(e + Vec3{0, 0.9f, 0}, e + side * 14.0f + Vec3{0, -1.5f, 0}, 0.025f, kRope);
                beam(e, e + side * 14.0f + Vec3{0, -2.4f, 0}, 0.4f, kPlankDark);
            }
        }
    }
    // Abajo: un mar de nubes que tapa el fondo, bancos de niebla entre los troncos y luciérnagas.
    addDecor(id, {0, -62, 10}, {420, 0.5f, 420}, rgba(132, 128, 156), Material::Cloth, false);
    for (int i = 0; i < 26; ++i) {
        float a = rng.range(0, 6.28f), r = rng.range(10, 120);
        Vec3 p{std::cos(a) * r, rng.range(-46, -30), std::sin(a) * r + 10};
        float s = rng.range(10, 22);
        addDecor(id, p, {s, s * 0.22f, s * 0.8f}, kMist, Material::Cloth, true);
    }
    for (int i = 0; i < 70; ++i) {
        float a = float(i) * 2.399f, r = 4.0f + float(i % 13) * 3.2f;
        Vec3 p{std::cos(a) * r, rng.range(-18, 4), std::sin(a) * r + 10};
        if (std::fabs(p.x) < 10.5f && std::fabs(p.z) < 10.5f && p.y > -1) continue;
        addDecor(id, p, {0.05f, 0.05f, 0.05f}, rgba(220, 255, 150), Material::Glow, true);
    }

    // --- Llegada, portal y objetivo --------------------------------------------------------
    heroStart_ = {7.0f, 0, 1.0f};  // de cara al tronco; detrás, la barandilla y el vacío
    heroStartYaw_ = -kPi * 0.5f;
    addPortal({-7.0f, 0, 0.5f});
    LevelPart climb{"Las Copas", "Sube al mirador de la copa más alta", PartKind::Reach, "", 0,
                    {{-3.3f, kC - 1, 30.2f}, {3.3f, kC + 4, 36.8f}}, {0, kC + 1.5f, 33.5f}, {}, "", ""};
    parts_ = {climb};
    nextLevel_ = LevelId::Copas;
}
