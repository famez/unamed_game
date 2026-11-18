// Ciudad de la Bahía, como un diorama: tres rincones pequeños y cerrados, a pocos pasos uno
// de otro, tallados en los morros. Lo lejano (el mar, el Pan de Azúcar con el teleférico, la
// favela en la ladera y la estatua del cerro) es solo telón de fondo: se ve, no se pisa.
//   - La Caleta: una playa recogida entre dos peñas; el cocotero del islote, en el centro.
//   - El Largo de los Arcos: una plaza pequeña cerrada por casas, con los arcos y el tranvía
//     al fondo.
//   - La Ladeira y el Mirador: un callejón por detrás de las casas, la escalera de azulejos y
//     un mirador con un almendro viejo sobre la caleta.
// El recinto (Terrain::fence) sigue el borde de las zonas; las peñas, las casas y las laderas
// lo tapan.

#include "game/game.h"
#include "game/map_tools.h"
#include "game/props.h"

#include <tuple>

using namespace maptools;

namespace {

// Callejón: tramos con su anchura y su altura en cada punto (la escalera, más estrecha).
struct LanePoint {
    float x, z, h, r;
};
const LanePoint kLane[] = {{-11, 12, 1.5f, 2.6f},  {-19.5f, 12, 2.2f, 2.6f}, {-23.5f, 8.5f, 3.0f, 2.4f}, {-23.5f, 7, 3.0f, 1.7f},
                           {-23.5f, -1, 7.0f, 1.7f}, {-23.5f, -2.5f, 7.0f, 2.4f}, {-25, -10, 8.6f, 2.4f},  {-26.5f, -14, 9.0f, 2.4f}};
const Vec3 kMirador{-28, 0, -19};
const float kMiradorR = 6.0f, kMiradorH = 9.0f;
const float kPlazaH = 1.5f;

// Distancia con signo al callejón y su altura en el punto más cercano.
float laneDist(Vec3 p, float* h) {
    float best = 1e9f;
    for (size_t i = 0; i + 1 < std::size(kLane); ++i) {
        const LanePoint &a = kLane[i], &b = kLane[i + 1];
        Vec3 ab{b.x - a.x, 0, b.z - a.z}, ap{p.x - a.x, 0, p.z - a.z};
        float t = clampf(dot(ap, ab) / std::max(1e-4f, dot(ab, ab)), 0, 1);
        float d = length(ap - ab * t) - (a.r + (b.r - a.r) * t);
        if (d < best) {
            best = d;
            if (h) *h = a.h + (b.h - a.h) * t;
        }
    }
    return best;
}

float shoreZ(float x) { return -33.0f - 0.012f * x * x; }  // la caleta: los brazos salen al mar

// Arena que baja hasta el agua y el fondo del mar.
float coveHeight(float x, float z) {
    float s = shoreZ(x);
    if (z < s) return -0.6f - std::min(3.5f, (s - z) * 0.12f);
    return -0.6f + 2.1f * smoothstep01((z - s) / (-8.0f - s));
}

// Pared alrededor de una zona: sube `wall` metros en `run` y luego sigue la ladera.
float rise(float d, float wall, float run, float slope) {
    if (d <= 0) return 0;
    return wall * smoothstep01(d / run) + slope * std::max(0.0f, d - run);
}

const Vec3 kIslet{9, 0, -38.5f};

}  // namespace

void Game::buildRio() {
    levelName_ = "Ciudad de la Bahía";
    floorColor_ = rgba(70, 130, 66);
    floorMaterial_ = Material::Grass;
    night_ = 0.5f;  // el atardecer detenido sobre la bahía
    fogScale_ = 0.22f;
    darkScale_ = 0.6f;
    viewDistance_ = 320;
    const float kSize = 260;
    makeTerrain(kSize, 1.0f);
    Terrain& t = const_cast<Terrain&>(*world_.terrain);
    Rng rng{1565};

    // Zonas (distancias con signo): por donde se anda y lo que se talla en el relieve.
    auto dCove = [](Vec3 p) { return sdBox(p, {0, 0, -34}, 19, 27, 5); };  // la caleta, con su trozo de mar
    auto dBeach = [](Vec3 p) { return sdBox(p, {0, 0, -23}, 19, 16, 4); };  // hasta donde se mete en el agua
    auto dPlaza = [](Vec3 p) { return sdBox(p, {0, 0, 5}, 13, 12); };
    auto dMirador = [](Vec3 p) { return sdCircle(p, kMirador, kMiradorR); };
    auto walk = [&](Vec3 p) { return std::min({dBeach(p), dPlaza(p), laneDist(p, nullptr), dMirador(p)}); };
    const Vec3 pao{50, 0, -112}, urca{18, 0, -92}, cerro{-6, 0, 108};

    std::vector<uint8_t> zone(t.heights.size());  // qué zona manda en cada punto (para el color)
    t.fence.assign(t.heights.size(), 0);
    for (int iz = 0; iz < t.nz; ++iz)
        for (int ix = 0; ix < t.nx; ++ix) {
            float x = t.x0 + float(ix) * t.cell, z = t.z0 + float(iz) * t.cell;
            Vec3 p{x, 0, z};
            // Cada zona es un hueco en el monte: el relieve es lo más bajo de sus paredes.
            float laneH = 0, dl = laneDist(p, &laneH);
            float dc = dCove(p), dp = dPlaza(p), dm = dMirador(p), ds = z + 60;  // ds: mar abierto
            float cand[] = {coveHeight(x, z) + rise(dc, 10, 3.5f, 0.3f),
                            -4.1f + rise(ds, 10, 6, 0.3f),
                            kPlazaH + rise(dp, 3, 3, 0.55f),  // detrás de las casas, ladera
                            laneH + rise(dl, 6, 3, 0.4f),
                            kMiradorH + rise(dm, 4, 3, 0.4f)};
            int k = 0;
            for (int c = 1; c < 5; ++c)
                if (cand[c] < cand[k]) k = c;
            float h = cand[k];
            float dIn = std::min({dc, ds, dp, dl, dm});
            // El fondo: morros que crecen lejos de las zonas, con la estatua en el más alto.
            auto bump = [&](Vec3 c, float amp, float r) {
                float q = length(flat(p - c));
                return amp * std::exp(-q * q / (2 * r * r));
            };
            float hills = bump(cerro, 64, 26) + bump({48, 0, 72}, 30, 34) + bump({-74, 0, 38}, 34, 38) + bump({62, 0, -18}, 14, 22);
            hills += 4.0f * (fbm(x / 40.0f, z / 40.0f, 77) + 1.0f);
            h += hills * smoothstep01((dIn - 8) / 26);
            // El Pan de Azúcar y Urca: cúpulas de roca en la bahía.
            for (auto [c, amp, r] : {std::tuple<Vec3, float, float>{pao, 52.0f, 22.0f}, {urca, 22.0f, 16.0f}}) {
                Vec3 dq = flat(p - c);
                float q = std::sqrt(dq.x * dq.x + dq.z * dq.z * 0.64f);
                if (q < r) h = std::max(h, amp * std::pow(1.0f - (q / r) * (q / r), 0.55f)), k = 5;
            }
            float qi = length(flat(p - kIslet));  // el islote del cocotero
            if (qi < 3.4f) h = std::max(h, -0.2f + 0.9f * (1 - (qi / 3.4f) * (qi / 3.4f)));
            float edge = std::min({x - t.x0, -t.x0 - x, -t.z0 - z});
            if (edge < 30) h += 30.0f * (1 - edge / 30) * (1 - edge / 30);
            size_t idx = size_t(iz) * size_t(t.nx) + size_t(ix);
            t.at(ix, iz) = h;
            t.fence[idx] = walk(p);
            zone[idx] = uint8_t(dIn <= 0.5f ? k : 6);
        }
    // Color: arena, losas, adoquines; fuera, roca en lo empinado y verde en lo demás.
    for (int iz = 0; iz < t.nz; ++iz)
        for (int ix = 0; ix < t.nx; ++ix) {
            float x = t.x0 + float(ix) * t.cell, z = t.z0 + float(iz) * t.cell;
            size_t idx = size_t(iz) * size_t(t.nx) + size_t(ix);
            float v = noise(x / 5.0f, z / 5.0f, 21) * 0.5f + 0.5f, cob = noise(x * 1.4f, z * 1.4f, 4) * 0.5f + 0.5f;
            float slope = 1.0f - t.normalAt(x, z).y;
            float h = t.at(ix, iz);
            uint32_t col;
            Material m = Material::Stone;
            switch (zone[idx]) {
                case 0:
                case 1:
                    col = mixRgb(rgba(236, 216, 168), rgba(222, 200, 150), v), m = Material::Cloth;
                    if (h < 0.1f) col = mixRgb(rgba(190, 172, 132), col, clampf((h + 0.6f) / 0.7f, 0, 1));  // arena mojada
                    break;
                case 2: col = mixRgb(rgba(214, 206, 190), rgba(190, 182, 168), cob); break;  // losas claras
                case 3: col = mixRgb(rgba(150, 132, 112), rgba(118, 104, 90), cob); break;   // adoquines
                case 4: col = mixRgb(rgba(200, 186, 160), rgba(176, 162, 140), cob); break;
                default:
                    col = mixRgb(rgba(48, 104, 52), rgba(78, 138, 62), v), m = Material::Grass;
                    if (slope > 0.3f) col = mixRgb(col, mixRgb(rgba(118, 108, 98), rgba(150, 140, 126), v), clampf((slope - 0.3f) * 4, 0, 1)),
                    m = Material::Stone;
                    if (h < 0.3f) col = rgba(190, 172, 132), m = Material::Cloth;  // fondo del mar
            }
            paintTerrain(ix, iz, col, m);
        }
    waterLevel_ = -0.25f;
    auto ground = [&](float x, float z) { return t.heightAt(x, z); };

    // --- La Caleta -------------------------------------------------------------------------
    // El punto focal: un cocotero alto e inclinado en el islote, al que se llega vadeando.
    addProp("cocotero_alto", kIslet + Vec3{-0.6f, 0, 0.4f}, 2.6f, 1.05f);
    for (int i = 0; i < 5; ++i) {
        float a = float(i) * 1.3f + 0.4f;
        addProp(i % 2 ? "roca" : "piedra", kIslet + Vec3{std::cos(a) * 2.6f, 0, std::sin(a) * 2.6f}, a, rng.range(0.8f, 1.3f));
    }
    // Las peñas que cierran la playa: rocas grandes al pie y metidas en el agua.
    for (float s : {-1.0f, 1.0f}) {
        for (float z = -10; z > -58; z -= 4.5f) {
            float x = s * (20.5f + rng.range(0, 2.5f) + std::max(0.0f, -z - 34) * 0.15f);
            addProp(rng.next() < 0.6f ? "roca_grande" : "roca", {x, 0, z}, rng.range(0, 6.28f), rng.range(1.0f, 1.8f));
        }
    }
    addProp("quiosco_playa", {-12, 0, -13}, kPi);
    addProp("banca_parque", {-8, 0, -11.5f}, kPi);
    addProp("puesto_socorrista", {5, 0, -21}, kPi);
    addProp("barca", {-7, 0, -31}, 1.2f);
    addProp("barca_roja", {14, 0, -29.5f}, 2.0f);
    for (Vec3 p : {Vec3{-16.5f, 0, -11}, Vec3{16.5f, 0, -13}, Vec3{-17.5f, 0, -23}})
        addProp(p.x < 0 ? "cocotero" : "cocotero_alto", p, rng.range(0, 6.28f));
    for (int i = 0; i < 6; ++i)  // sombrillas cerradas: la playa vacía
        addProp(i % 2 ? "sombrilla_cerrada" : "sombrilla_cerrada_azul", {-4.0f + float(i) * 3.4f, 0, -16.0f - float(i % 3) * 2.5f},
                rng.range(0, 6.28f));
    // El paseo de ondas entre la playa y la plaza, con dos farolas.
    for (float x : {-12.0f, 0.0f, 12.0f}) addProp("paseo_ondas", {x, kPlazaH, -5.5f}, 0, 1.0f, false);
    for (float x : {-8.0f, 8.0f}) addProp("farola_paseo", {x, 0, -8.2f}, 0);

    // --- El Largo de los Arcos -----------------------------------------------------------
    // Casas a los dos lados, de cara a la plaza (bajas: la plaza manda); hueco al oeste para
    // el callejón.
    auto house = [&](const char* name, float x, float z, float scl = 1.0f) {
        addProp(name, {x, 0, z}, x < 0 ? kPi * 0.5f : -kPi * 0.5f, scl);
    };
    house("casa_favela_5", -15.6f, -3);
    house("casa_favela_3", -15.4f, 2);
    house("casa_favela_6", -15.6f, 7);
    house("casa_favela_1", -15.4f, 16.5f);
    house("edificio_playa_azul", 17, -3, 0.34f);
    house("casa_favela_4", 15.6f, 3);
    house("casa_favela_8", 15.4f, 8);
    house("edificio_playa_rosa", 17, 13.5f, 0.3f);
    // El punto focal: los arcos, con el tranvía parado encima; por ellos se ve el morro.
    const float kArc = 0.65f;
    addProp("arcos", {0, kPlazaH, 18.6f}, 0, kArc, false);
    addProp("tranvia_amarillo", {3.5f, kPlazaH + 15.2f * kArc, 18.6f}, kPi * 0.5f, kArc, false);
    addProp("almendro_playa", {-8.5f, 0, 12}, 0.5f);
    addProp("almendro_playa", {9, 0, 11.5f}, 2.1f);
    for (float x : {-4.5f, 4.5f}) addProp("banca_parque", {x, 0, 10.5f}, kPi);
    for (Vec3 p : {Vec3{-11, 0, -1}, Vec3{11, 0, -1}, Vec3{-11, 0, 15}, Vec3{11, 0, 15}}) addProp("farol_colonial", p);
    addProp("puesto_comida", {10.5f, 0, 5}, -kPi * 0.5f);

    // --- La Ladeira y el Mirador ---------------------------------------------------------
    // La escalera sube hacia el sur (hacia el mar), por detrás de las casas de la plaza.
    addProp("escalera_azulejos", {-23.5f, 3.0f, 3}, kPi, 1.0f, false);
    addProp("casa_favela_2", {-21, 0, 16.5f}, kPi);
    addProp("casa_favela_8", {-28.5f, 0, 6}, kPi * 0.5f);
    addProp("casa_favela_3", {-28.5f, 0, -1}, kPi * 0.5f);
    addProp("casa_favela_7", {-30.5f, 0, -9}, kPi * 0.5f);
    addProp("casa_favela_5", {-19.5f, 0, -8}, -kPi * 0.5f);
    for (Vec3 p : {Vec3{-21, 0, 9.5f}, Vec3{-21.6f, 0, -4}, Vec3{-23, 0, -12}}) addProp("farol_colonial", p);
    // El mirador: el almendro viejo, un banco mirando a la caleta y una baranda.
    addProp("almendro_playa", kMirador + Vec3{-3, 0, -1.5f}, 1.0f, 1.45f);
    addProp("banca_parque", kMirador + Vec3{2.5f, 0, 0.5f}, kPi * 0.5f);
    for (int i = -2; i <= 2; ++i) {
        float a = float(i) * 0.5f;
        Vec3 p = kMirador + Vec3{std::cos(a) * (kMiradorR - 0.4f), 0, std::sin(a) * (kMiradorR - 0.4f)};
        addProp("valla", p, -a - kPi * 0.5f, 1.0f);
    }
    addProp("farol_colonial", kMirador + Vec3{3.5f, 0, 4.5f});

    // --- El telón de fondo (no se pisa) ---------------------------------------------------
    // La favela trepando por el morro detrás de los arcos y al oeste.
    for (int i = 0; i < 400; ++i) {
        Vec3 p{rng.range(-80, 40), 0, rng.range(28, 95)};
        if (walk(p) < 7 || length(flat(p - cerro)) < 34) continue;
        float g = ground(p.x, p.z);
        if (g < 4) continue;
        Vec3 n = t.normalAt(p.x, p.z);  // de cara a la bahía, hacia abajo de la ladera
        float yaw = std::round(std::atan2(n.x, n.z) / (kPi * 0.5f)) * kPi * 0.5f + rng.range(-0.08f, 0.08f);
        addProp("casa_favela_" + std::to_string(1 + rng.pick(8)), p, yaw);
    }
    addProp("estatua_cerro", {cerro.x, 0, cerro.z}, kPi);
    // Selva en las laderas que rodean las zonas.
    for (int i = 0; i < 420; ++i) {
        Vec3 p{rng.range(-110, 110), 0, rng.range(-70, 110)};
        float g = ground(p.x, p.z);
        if (walk(p) < 3.5f || g < 1.5f || (p.x > -82 && p.x < 42 && p.z > 26 && p.z < 97 && rng.next() < 0.75f)) continue;
        float r = rng.next();
        addProp(r < 0.45f ? "almendro_playa" : r < 0.7f ? "platanera" : r < 0.88f ? "cocotero" : "guadual", p, rng.range(0, 6.28f),
                rng.range(0.8f, 1.3f));
    }
    // Teleférico: de la peña del este al morro de Urca y al Pan de Azúcar.
    Vec3 station{27, 0, -47};
    station.y = ground(station.x, station.z);
    Vec3 topU = urca + Vec3{0, ground(urca.x, urca.z), 0}, topP = pao + Vec3{0, ground(pao.x, pao.z), 0};
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
        for (Vec3 e : {a, b}) addDecor(Mat4::identity(), e + Vec3{0, 4.5f, 0}, {2.2f, 4.5f, 2.2f}, rgba(226, 222, 214), Material::Stone, false);
    };
    cable(station, topU);
    cable(topU, topP);
    Vec3 mid1 = station + (topU - station) * 0.55f, mid2 = topU + (topP - topU) * 0.4f;
    addProp("teleferico", mid1 + Vec3{1.6f, 9 - 3.7f, 0}, std::atan2(topU.x - station.x, topU.z - station.z), 1.0f, false);
    addProp("teleferico", mid2 + Vec3{-1.6f, 9 - 3.7f, 0}, std::atan2(topP.x - topU.x, topP.z - topU.z), 1.0f, false);

    // --- Habitantes, portales y nodos ----------------------------------------------------
    addNpc("vendedora", {-11, 0, -10.5f}, 2);
    addNpc("barquero", {-5, 0, -28}, 4);
    addNpc("oficinista", {3, 0, 6}, 4);
    addNpc("pregonero", {-6, 0, 2}, 3);
    addNpc("musica", {2.0f, 0, 16.2f}, 1);  // toca bajo los arcos
    addNpc("trapera", {-20, 0, 10.5f}, 2);
    addNpc("relojero", kMirador + Vec3{1, 0, -2}, 2);
    heroStart_ = {0, kPlazaH, -4};
    heroStartYaw_ = 0;  // de cara a la plaza y a los arcos; la caleta, a la espalda
    addPortal({7.5f, kPlazaH, 2});
    addPortal(kMirador + Vec3{0.5f, kMiradorH, 3});
    // Nodos de color: el islote, la plaza, lo alto de la escalera y el mirador.
    const Vec3 nodeSpots[] = {kIslet + Vec3{1.6f, 0, 1.4f}, {-5, 0, 14}, {-23.5f, 0, -3.5f}, kMirador + Vec3{-0.5f, 0, -3}};
    const uint32_t nodeColors[] = {rgba(255, 190, 60), rgba(40, 220, 255), rgba(255, 70, 190), rgba(120, 255, 140)};
    for (int k = 0; k < 4; ++k) {
        Vec3 p = nodeSpots[k];
        p.y = ground(p.x, p.z);
        nodes_.push_back({"nodo_bahia_" + std::to_string(k), p, 24, nodeColors[k], ""});
    }
    LevelPart explore{"Ciudad de la Bahía", "Enciende los cuatro nodos de la bahía", PartKind::Nodes, "", 4, {}, nodeSpots[0], {}, "", ""};
    parts_ = {explore};
    nextLevel_ = LevelId::Rio;
}
