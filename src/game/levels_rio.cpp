// Ciudad de la Bahía (inspirada en Río de Janeiro), de día: una playa en media luna con el
// paseo de ondas, la avenida y los edificios frente al mar; detrás, manzanas, los arcos con
// el tranvía y, en los morros, la favela (al oeste) y la estatua de brazos abiertos en lo alto
// del cerro (al norte). En la bahía, el Pan de Azúcar y el morro de Urca, unidos por el
// teleférico.

#include "game/game.h"
#include "game/map_tools.h"
#include "game/props.h"

#include <tuple>

using namespace maptools;

void Game::buildRio() {
    levelName_ = "Ciudad de la Bahía";
    floorColor_ = rgba(70, 130, 66);
    floorMaterial_ = Material::Grass;
    night_ = 0.5f;  // el atardecer detenido sobre la bahía
    fogScale_ = 0.22f;
    darkScale_ = 0.6f;
    viewDistance_ = 460;
    const float kSize = 420;
    makeTerrain(kSize, 2.0f);
    Terrain& t = const_cast<Terrain&>(*world_.terrain);
    Rng rng{1565};

    // La orilla: media luna (el centro de la playa, más hacia dentro).
    auto shoreZ = [](float x) { return -78.0f + 26.0f * std::cos(x / 210.0f * kPi); };
    const float kSand = 42;   // ancho de la arena
    const float kWalk = 7;    // paseo de ondas
    const float kAve = 14;    // avenida
    auto inland = [&](Vec3 p) { return p.z - shoreZ(p.x); };  // metros desde la orilla hacia dentro
    const Vec3 favela{-118, 0, 92}, cerro{58, 0, 150}, pao{150, 0, -150}, urca{112, 0, -118};

    for (int iz = 0; iz < t.nz; ++iz)
        for (int ix = 0; ix < t.nx; ++ix) {
            float x = t.x0 + float(ix) * t.cell, z = t.z0 + float(iz) * t.cell;
            Vec3 p{x, 0, z};
            float d = inland(p);
            float h;
            uint32_t col;
            Material m = Material::Grass;
            float v = noise(x / 6.0f, z / 6.0f, 21) * 0.5f + 0.5f;
            if (d < 0) {  // mar: el fondo baja
                h = -0.6f - std::min(4.0f, -d * 0.12f);
                col = rgba(196, 178, 136), m = Material::Cloth;
            } else if (d < kSand) {  // arena, subiendo suave hasta el paseo
                h = -0.6f + 1.8f * smoothstep01(d / kSand);
                col = mixRgb(rgba(236, 216, 168), rgba(222, 200, 150), v), m = Material::Cloth;
                if (d < 4) col = mixRgb(rgba(196, 178, 136), col, d / 4);  // arena mojada
            } else {  // ciudad llana y, detrás, los morros
                h = 1.2f;
                float city = d - kSand;
                col = city < kWalk + kAve ? rgba(64, 66, 72) : mixRgb(rgba(150, 146, 138), rgba(128, 124, 118), v);  // avenida y aceras
                m = Material::Stone;
                float hill = 0;
                auto bump = [&](Vec3 c, float amp, float r) {
                    float q = length(flat(p - c));
                    return amp * std::exp(-q * q / (2 * r * r));
                };
                hill += bump(favela, 46, 52) + bump(cerro, 82, 34) + bump(cerro + Vec3{-60, 0, 30}, 40, 45) + bump({-30, 0, 190}, 50, 60);
                hill += 8.0f * (fbm(x / 50.0f, z / 50.0f, 77) + 1.0f) * smoothstep01((z - 30) / 80);
                float edge = std::min({x - t.x0, -t.x0 - x, -t.z0 - z});
                if (edge < 40) hill += 35.0f * (1 - edge / 40) * (1 - edge / 40);
                hill *= smoothstep01((city - 50) / 60);  // la ciudad llana junto al mar
                h += hill;
                if (hill > 3) {  // monte: selva atlántica, roca en lo más empinado
                    col = mixRgb(rgba(46, 104, 50), rgba(70, 132, 60), v), m = Material::Grass;
                    if (hill > 60) col = mixRgb(col, rgba(120, 116, 106), clampf((hill - 60) / 15, 0, 1)), m = Material::Stone;
                }
            }
            // El Pan de Azúcar y Urca: cúpulas de roca que salen del agua, con algo de verde arriba.
            for (auto [c, amp, r] : {std::tuple<Vec3, float, float>{pao, 52.0f, 24.0f}, {urca, 24.0f, 20.0f}}) {
                Vec3 dq = flat(p - c);
                float q = std::sqrt(dq.x * dq.x + dq.z * dq.z * 0.64f);
                if (q < r) {
                    float dome = amp * std::pow(1.0f - (q / r) * (q / r), 0.55f);
                    if (dome > h) {
                        h = dome;
                        col = dome > amp * 0.8f ? rgba(64, 120, 58) : mixRgb(rgba(110, 104, 98), rgba(140, 134, 126), v);
                        m = dome > amp * 0.8f ? Material::Grass : Material::Stone;
                    }
                }
            }
            t.at(ix, iz) = h;
            paintTerrain(ix, iz, col, m);
        }
    waterLevel_ = -0.25f;
    auto ground = [&](float x, float z) { return t.heightAt(x, z); };
    auto along = [&](float x, float off) {  // punto a `off` m de la orilla, hacia dentro, y la orientación de la costa
        float dx = 1.0f, dz = shoreZ(x + 0.5f) - shoreZ(x - 0.5f);
        Vec3 tan = normalize(Vec3{dx, 0, dz}), in{-tan.z, 0, tan.x};
        return std::pair<Vec3, float>{Vec3{x, 0, shoreZ(x)} + in * off, std::atan2(in.x, in.z)};
    };

    // Paseo de ondas, farolas, cocoteros y quioscos.
    for (float x = -180; x <= 180; x += 12) {
        auto [p, yaw] = along(x, kSand + kWalk * 0.5f);
        addProp("paseo_ondas", p, yaw, 1.0f);
    }
    int k = 0;
    for (float x = -176; x <= 176; x += 22, ++k) {
        auto [p, yaw] = along(x, kSand + kWalk - 0.6f);
        addProp("farola_paseo", p, yaw);
        auto [q, yq] = along(x + 11, kSand - 2.5f);
        addProp(k % 2 ? "cocotero" : "cocotero_alto", q, rng.range(0, 6.28f));
        if (k % 4 == 1) {
            auto [r, yr] = along(x + 5, kSand - 9);
            addProp(k % 8 == 1 ? "quiosco_playa" : "quiosco_playa_azul", r, yr);
        }
        if (k % 4 == 3) {
            auto [r, yr] = along(x, 12);
            addProp("puesto_socorrista", r, yr + kPi);
        }
    }
    for (int i = 0; i < 70; ++i) {  // sombrillas por la arena
        float x = rng.range(-170, 170);
        auto [p, yaw] = along(x, rng.range(9, kSand - 14));
        float r = rng.next();  // casi todas cerradas: la playa vacía
        addProp(r < 0.4f ? "sombrilla_cerrada" : r < 0.8f ? "sombrilla_cerrada_azul" : r < 0.9f ? "sombrilla_playa" : "sombrilla_playa_amarilla", p,
                yaw + rng.range(-0.5f, 0.5f));
    }
    for (float x : {-175.0f, -168.0f, 166.0f, 173.0f}) {  // barcas en los extremos
        auto [p, yaw] = along(x, 5);
        addProp(x < 0 ? "barca" : "barca_roja", p, yaw + 1.3f);
    }
    // Edificios frente al mar, mirando a la playa.
    const char* fronts[] = {"edificio_playa_blanco", "edificio_playa_crema", "edificio_playa_rosa", "edificio_playa_azul"};
    for (float x = -170; x <= 170; x += 21) {
        auto [p, yaw] = along(x, kSand + kWalk + kAve + 8);
        addProp(fronts[rng.pick(4)], p, yaw + kPi, 1.0f);
    }
    // Manzanas detrás: edificios más bajos, almendros en las aceras.
    for (float x = -170; x <= 170; x += 26)
        for (float z = -40; z <= 140; z += 24) {
            Vec3 c{x + rng.range(-2, 2), 0, z + rng.range(-2, 2)};
            if (inland(c) < kSand + kWalk + kAve + 22 || ground(c.x, c.z) > 6 || std::fabs(c.z - 62) < 14) continue;
            float scl = rng.range(0.45f, 0.75f);  // más bajos que los del paseo
            addProp(fronts[rng.pick(4)], c, float(rng.pick(4)) * kPi * 0.5f, scl);
            if (rng.next() < 0.7f) addProp("almendro_playa", c + Vec3{9.5f, 0, 8.5f}, rng.range(0, 6.28f));
            if (rng.next() < 0.4f) addProp("farola_paseo", c + Vec3{-9.5f, 0, 8.5f}, 0);
        }
    // Los arcos con el tranvía encima, en el centro.
    for (float x : {-15.0f, 15.0f}) addProp("arcos", {x, 0, 62}, 0, 1.0f);
    addProp("tranvia_amarillo", {6, ground(6, 62) + 15.2f, 62}, kPi * 0.5f, 1.0f, false);
    // La favela: casas apiñadas en la ladera del morro, con escaleras de azulejos.
    for (int i = 0; i < 260; ++i) {
        float a = rng.range(0, 6.28f), r = std::sqrt(rng.next()) * 62;
        Vec3 p = favela + Vec3{std::cos(a) * r, 0, std::sin(a) * r * 0.85f};
        float g = ground(p.x, p.z);
        if (g < 5 || g > 42) continue;
        // De cara a la bahía (hacia abajo de la ladera).
        Vec3 n = t.normalAt(p.x, p.z);
        float yaw = std::atan2(n.x, n.z) + rng.range(-0.25f, 0.25f);
        yaw = std::round(yaw / (kPi * 0.5f)) * kPi * 0.5f + rng.range(-0.08f, 0.08f);
        addProp("casa_favela_" + std::to_string(1 + rng.pick(8)), p, yaw);
    }
    for (int i = 0; i < 4; ++i) {  // escaleras subiendo el morro
        Vec3 p = favela + Vec3{-20.0f + float(i) * 14, 0, -38.0f + float(i) * 6};
        Vec3 n = t.normalAt(p.x, p.z);
        addProp("escalera_azulejos", p, std::atan2(-n.x, -n.z));
    }
    // El cerro: la estatua mirando a la ciudad y selva en las laderas.
    addProp("estatua_cerro", {cerro.x, 0, cerro.z}, kPi);
    for (int i = 0; i < 520; ++i) {
        Vec3 p{rng.range(-200, 200), 0, rng.range(-30, 200)};
        float g = ground(p.x, p.z);
        if (g < 8 || length(flat(p - favela)) < 64 || length(flat(p - cerro)) < 10) continue;
        float r = rng.next();
        addProp(r < 0.45f ? "almendro_playa" : r < 0.7f ? "platanera" : r < 0.85f ? "cocotero" : "guadual", p, rng.range(0, 6.28f),
                rng.range(0.8f, 1.3f));
    }
    // Teleférico: estaciones en las cumbres, cables y dos cabinas.
    Vec3 topU = urca + Vec3{0, ground(urca.x, urca.z), 0}, topP = pao + Vec3{0, ground(pao.x, pao.z), 0};
    Vec3 shore = Vec3{88, 0, 0};
    shore.z = shoreZ(88) + kSand + 2;
    shore.y = ground(shore.x, shore.z);
    auto cable = [&](Vec3 a, Vec3 b) {
        for (float s : {-1.6f, 1.6f}) {
            Vec3 side = normalize(Vec3{b.z - a.z, 0, a.x - b.x}) * s;
            Vec3 aa = a + side + Vec3{0, 9, 0}, bb = b + side + Vec3{0, 9, 0}, d = bb - aa;
            Vec3 y = normalize(d), ref{0, 1, 0};
            Vec3 x = normalize(cross(ref, y)), z = cross(x, y);
            Mat4 m = Mat4::identity();
            m.m[0] = x.x, m.m[1] = x.y, m.m[2] = x.z, m.m[4] = y.x, m.m[5] = y.y, m.m[6] = y.z, m.m[8] = z.x, m.m[9] = z.y, m.m[10] = z.z;
            m.m[12] = (aa.x + bb.x) / 2, m.m[13] = (aa.y + bb.y) / 2, m.m[14] = (aa.z + bb.z) / 2;
            addDecor(m, {}, {0.04f, length(d) * 0.5f, 0.04f}, rgba(40, 40, 44), Material::Metal, false);
        }
        for (Vec3 e : {a, b}) {  // torres de las estaciones
            addDecor(Mat4::identity(), e + Vec3{0, 4.5f, 0}, {2.2f, 4.5f, 2.2f}, rgba(226, 222, 214), Material::Stone, false);
            propSolids_.push_back(boxFromCenter(e + Vec3{0, 4.5f, 0}, {2.2f, 4.5f, 2.2f}));
        }
    };
    cable(shore, topU);
    cable(topU, topP);
    Vec3 mid1 = shore + (topU - shore) * 0.45f, mid2 = topU + (topP - topU) * 0.6f;
    float yaw1 = std::atan2(topU.x - shore.x, topU.z - shore.z), yaw2 = std::atan2(topP.x - topU.x, topP.z - topU.z);
    addProp("teleferico", mid1 + Vec3{1.6f, 9 - 3.7f, 0}, yaw1, 1.0f, false);
    addProp("teleferico", mid2 + Vec3{-1.6f, 9 - 3.7f, 0}, yaw2, 1.0f, false);

    // Habitantes: pocos y por el paseo (la playa, vacía); alguna vendedora que recoge.
    for (int i = 0; i < 10; ++i) {
        auto [p, yaw] = along(rng.range(-160, 160), kSand + kWalk * 0.5f);
        addNpc(i % 3 == 0 ? "abuelo" : i % 3 == 1 ? "oficinista" : "vendedora", p, 14);
    }
    // Portales y llegada: en el paseo, mirando al mar y al Pan de Azúcar.
    auto [start, startYaw] = along(-10, kSand + kWalk * 0.5f);
    start.y = ground(start.x, start.z);
    heroStart_ = start;
    heroStartYaw_ = startYaw + kPi;
    auto [pa, ya] = along(20, kSand + 3);
    addPortal({pa.x, ground(pa.x, pa.z), pa.z});
    addPortal({favela.x + 30, ground(favela.x + 30, favela.z - 40), favela.z - 40});
    // Nodos de color: la playa, la favela, los arcos y el cerro de la estatua.
    auto [nb, nby] = along(30, kSand + 3);
    const Vec3 nodeSpots[] = {nb, favela + Vec3{18, 0, -30}, {0, 0, 50}, cerro + Vec3{0, 0, -9}};
    const uint32_t nodeColors[] = {rgba(255, 190, 60), rgba(255, 70, 190), rgba(40, 220, 255), rgba(120, 255, 140)};
    for (int k = 0; k < 4; ++k) {
        Vec3 p = nodeSpots[k];
        p.y = ground(p.x, p.z);
        nodes_.push_back({"nodo_bahia_" + std::to_string(k), p, 40, nodeColors[k], ""});
    }
    LevelPart explore{"Ciudad de la Bahía", "Enciende los cuatro nodos de la bahía", PartKind::Nodes, "", 4, {}, nodeSpots[0], {}, "", ""};
    parts_ = {explore};
    (void)nby;
    nextLevel_ = LevelId::Rio;
    (void)ya;
}
