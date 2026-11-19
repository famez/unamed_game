// Ciudad de la Bahía: una ciudad colonial que trepa por los morros sobre una bahía salvaje.
// Todo se anda por calles, plazas y escaleras talladas en el relieve; lo de fuera (los morros
// con la selva y la favela, el mar, el Pan de Azúcar y la estatua del cerro) se ve, no se pisa.
//   - La Caleta: playa salvaje entre dos peñas, sin nada de nadie; el cocotero del islote.
//   - El Largo de los Arcos: la plaza de las casas bajas, con los arcos y el tranvía.
//   - La Ladeira y el Mirador: el callejón, la escalera de azulejos y el almendro viejo.
//   - La Rua dos Arcos y la Praça do Coreto: se cruza bajo los arcos al centro, con su quiosco
//     de música, la fuente y calles de sobrados a los lados.
//   - Santa Teresa: la cuesta en zigzag hasta el Largo da Igreja, y la Rua Monte Alegre que
//     vuelve por el Largo dos Guimarães y la Rua do Lavradio.
//   - El Morro: los becos de la favela que suben a la cancha y bajan al mirador.
//   - La Rua da Praia y el Largo do Bondinho: la estación del teleférico sobre la peña; de allí
//     una senda por la selva baja a la Prainha, una playa virgen con su laguna.
// El recinto (Terrain::fence) sigue el borde de las zonas; las casas y las laderas lo tapan.

#include "game/game.h"
#include "game/map_tools.h"
#include "game/props.h"

#include <tuple>

using namespace maptools;

namespace {

// Calles: tramos con su anchura (r) y su altura (h) en cada punto.
struct LanePoint {
    float x, z, h, r;
};
enum Look : uint8_t { Sand, Losas, Adoquines, PiedraClara, Cancha, Tierra, Fuera };
struct Lane {
    std::vector<LanePoint> pts;
    Look look;
    float wall = 4, run = 3, slope = 0.45f;
};

// Distancia con signo a una calle y su altura en el punto más cercano.
float laneDist(const Lane& l, Vec3 p, float* h, float* r = nullptr) {
    float best = 1e9f;
    for (size_t i = 0; i + 1 < l.pts.size(); ++i) {
        const LanePoint &a = l.pts[i], &b = l.pts[i + 1];
        Vec3 ab{b.x - a.x, 0, b.z - a.z}, ap{p.x - a.x, 0, p.z - a.z};
        float t = clampf(dot(ap, ab) / std::max(1e-4f, dot(ab, ab)), 0, 1);
        float rr = a.r + (b.r - a.r) * t;
        float d = length(ap - ab * t) - rr;
        if (d < best) {
            best = d;
            if (h) *h = a.h + (b.h - a.h) * t;
            if (r) *r = rr;
        }
    }
    return best;
}

// Plazas y explanadas: caja (redondeada) o círculo a una altura. Las que no se andan solo
// allanan el relieve (bajo la estación, la laguna).
struct Area {
    Vec3 c;
    float hx, hz, round;  // hz < 0: círculo de radio hx
    float h;
    Look look;
    bool walk = true;
    float wall = 4, run = 3, slope = 0.45f;
    float dist(Vec3 p) const { return hz < 0 ? sdCircle(p, c, hx) : sdBox(p, c, hx, hz, round); }
};

float shoreZ(float x) { return -33.0f - 0.012f * x * x; }  // la caleta: los brazos salen al mar
float prainhaShore(float x) { return -44.0f - 0.004f * (x - 90) * (x - 90); }

// Arena que baja hasta el agua y el fondo del mar (orilla en `s`, arriba a `top` metros).
float sandHeight(float z, float s, float top) {
    if (z < s) return -0.6f - std::min(3.5f, (s - z) * 0.12f);
    return -0.6f + 2.1f * smoothstep01((z - s) / std::max(1.0f, top - s));
}

// Pared alrededor de una zona: sube `wall` metros en `run` y luego sigue la ladera.
float rise(float d, float wall, float run, float slope) {
    if (d <= 0) return 0;
    return wall * smoothstep01(d / run) + slope * std::max(0.0f, d - run);
}

const Vec3 kIslet{9, 0, -38.5f};
const Vec3 kMirador{-28, 0, -19};
const float kMiradorR = 6.0f, kMiradorH = 9.0f;
const float kPlazaH = 1.5f;
const Vec3 kCoreto{0, 0, 48}, kIgreja{40, 0, 98}, kBondinho{50, 0, -14}, kQuadra{-58, 0, 0}, kGuimaraes{-14, 0, 92};
const float kCoretoH = 2.5f, kIgrejaH = 15.0f, kBondinhoH = 3.5f, kQuadraH = 12.5f, kGuimaraesH = 8.5f;

// Rectángulo orientado (las casas), para no ponerlas unas encima de otras.
struct Footprint {
    Vec3 c;
    float yaw, hw, hd;
    bool contains(Vec3 p, float margin = 0) const {
        Vec3 d = flat(p - c);
        float cs = std::cos(yaw), sn = std::sin(yaw);
        float lx = d.x * cs - d.z * sn, lz = d.x * sn + d.z * cs;
        return std::fabs(lx) < hw + margin && std::fabs(lz) < hd + margin;
    }
    Vec3 corner(float sx, float sz) const {
        float cs = std::cos(yaw), sn = std::sin(yaw);
        float lx = sx * hw, lz = sz * hd;
        return c + Vec3{lx * cs + lz * sn, 0, -lx * sn + lz * cs};
    }
};

}  // namespace

void Game::buildRio() {
    levelName_ = "Ciudad de la Bahía";
    floorColor_ = rgba(70, 130, 66);
    floorMaterial_ = Material::Grass;
    night_ = 0.5f;  // el atardecer detenido sobre la bahía
    fogScale_ = 0.22f;
    darkScale_ = 0.6f;
    viewDistance_ = 280;
    const float kSize = 340;
    makeTerrain(kSize, 1.0f);
    Terrain& t = const_cast<Terrain&>(*world_.terrain);
    Rng rng{1565};

    // --- Zonas -------------------------------------------------------------------------------
    const std::vector<Lane> lanes = {
        // La Ladeira: el callejón por detrás de las casas, la escalera y la subida al mirador.
        {{{-11, 12, 1.5f, 2.6f}, {-19.5f, 12, 2.2f, 2.6f}, {-23.5f, 8.5f, 3.0f, 2.4f}, {-23.5f, 7, 3.0f, 1.7f},
          {-23.5f, -1, 7.0f, 1.7f}, {-23.5f, -2.5f, 7.0f, 2.4f}, {-25, -10, 8.6f, 2.4f}, {-26.5f, -14, 9.0f, 2.4f}},
         Adoquines, 6, 3, 0.4f},
        // Rua dos Arcos: de la plaza, bajo el arco central, a la Praça do Coreto.
        {{{0, 14, kPlazaH, 3.5f}, {0, 37, kCoretoH, 3.5f}}, Adoquines},
        // Rua do Ouvidor (al oeste, hacia el morro) y Rua Larga (al este, hacia Santa Teresa).
        {{{-14, 46, kCoretoH, 3.2f}, {-40, 46, 4.5f, 3.2f}, {-46, 40, 5.5f, 2.6f}}, Adoquines},
        {{{14, 46, kCoretoH, 3.2f}, {40, 46, 4.0f, 3.2f}}, Adoquines},
        // Ladeira de Santa Teresa: en zigzag hasta el Largo da Igreja.
        {{{40, 46, 4.0f, 3.0f}, {46, 58, 7.0f, 3.0f}, {36, 68, 10.0f, 3.0f}, {44, 80, 13.0f, 3.0f}, {40, 90, kIgrejaH, 3.0f}},
         Adoquines},
        // Rua do Lavradio y Rua Monte Alegre: la vuelta por el Largo dos Guimarães.
        {{{-8, 58, kCoretoH, 3.0f}, {-22, 70, 4.5f, 3.0f}, {-20, 84, 7.0f, 3.0f}, {-16, 89, kGuimaraesH, 3.0f}}, Adoquines},
        {{{-8, 93, kGuimaraesH, 3.0f}, {10, 95, 11.0f, 3.0f}, {28, 97, kIgrejaH, 3.0f}}, Adoquines},
        // Los becos del morro: del Ouvidor a la cancha y de la cancha al mirador.
        {{{-46, 40, 5.5f, 2.2f}, {-54, 30, 8.0f, 2.2f}, {-62, 20, 10.5f, 2.2f}, {-58, 8, kQuadraH, 2.2f}, {-58, 3, kQuadraH, 2.2f}}, Tierra, 5, 3, 0.4f},
        {{{-52, -3, kQuadraH, 2.0f}, {-42, -10, 11.0f, 2.0f}, {-32, -17, kMiradorH, 2.0f}}, Tierra, 5, 3, 0.4f},
        // Rua da Praia: de la plaza, por encima de la caleta, al Largo do Bondinho.
        {{{12, -7.5f, kPlazaH, 3.0f}, {26, -9.5f, 2.2f, 3.0f}, {42, -12, kBondinhoH, 3.0f}}, Adoquines},
        // La senda de la Prainha: baja por la selva desde el largo.
        {{{57, -13, kBondinhoH, 2.2f}, {66, -17, 2.6f, 2.2f}, {74, -24, 1.8f, 2.2f}, {80, -31, 1.5f, 2.4f}}, Tierra, 5, 3, 0.4f},
    };
    const Vec3 dirUrca = normalize(flat(Vec3{18, 0, -92} - kBondinho));
    const Vec3 stationAt = kBondinho + dirUrca * 15.5f;
    const std::vector<Area> areas = {
        {{0, 0, 5}, 13, 12, 0, kPlazaH, Losas, true, 3, 3, 0.55f},  // el Largo de los Arcos
        {kMirador, kMiradorR, -1, 0, kMiradorH, PiedraClara},
        {kCoreto, 15, 12, 0, kCoretoH, Losas},
        {kIgreja, 14, 9, 2, kIgrejaH, PiedraClara},
        {kGuimaraes, 7.5f, -1, 0, kGuimaraesH, PiedraClara},
        {kQuadra, 9, 6, 0, kQuadraH, Cancha},
        {kBondinho, 10, -1, 0, kBondinhoH, Losas},
        {stationAt, 7, -1, 0, kBondinhoH, Fuera, false},          // bajo la estación
        {{96, 0, -24}, 5.5f, -1, 0, -1.4f, Sand, false, 1.5f, 2.5f, 0.3f},  // la laguna de la Prainha
    };
    auto dBeach = [](Vec3 p) { return sdBox(p, {0, 0, -23}, 19, 16, 4); };       // la caleta, hasta el agua
    auto dCove = [](Vec3 p) { return sdBox(p, {0, 0, -34}, 19, 27, 5); };        // con su trozo de mar
    auto dPrainha = [](Vec3 p) { return sdBox(p, {90, 0, -38}, 18, 8, 3); };      // la playa virgen
    auto dPrainhaSea = [](Vec3 p) { return sdBox(p, {90, 0, -46}, 20, 16, 5); };
    auto walk = [&](Vec3 p) {
        float d = std::min(dBeach(p), dPrainha(p));
        for (const Lane& l : lanes) d = std::min(d, laneDist(l, p, nullptr));
        for (const Area& a : areas)
            if (a.walk) d = std::min(d, a.dist(p));
        return d;
    };
    const Vec3 pao{50, 0, -112}, urca{18, 0, -92}, cerro{-15, 0, 145};

    std::vector<uint8_t> zone(t.heights.size());  // qué zona manda en cada punto (para el color)
    t.fence.assign(t.heights.size(), 0);
    for (int iz = 0; iz < t.nz; ++iz)
        for (int ix = 0; ix < t.nx; ++ix) {
            float x = t.x0 + float(ix) * t.cell, z = t.z0 + float(iz) * t.cell;
            Vec3 p{x, 0, z};
            // Cada zona es un hueco en el monte: el relieve es lo más bajo de sus paredes; dentro
            // de lo que se anda manda la zona (la más baja si se tocan dos).
            float h = 1e9f, inside = 1e9f, dIn = 1e9f;
            Look look = Fuera, insideLook = Fuera;
            auto cand = [&](float d, float base, float wall, float run, float slope, Look lk, bool walkable) {
                dIn = std::min(dIn, d);
                if (walkable && d <= 0 && base < inside) inside = base, insideLook = lk;
                float c = base + rise(d, wall, run, slope);
                if (c < h) h = c, look = d <= 0.5f ? lk : Fuera;
            };
            cand(dCove(p), sandHeight(z, shoreZ(x), -8), 10, 3.5f, 0.3f, Sand, false);
            if (dBeach(p) <= 0) inside = std::min(inside, sandHeight(z, shoreZ(x), -8)), insideLook = Sand;
            cand(dPrainhaSea(p), sandHeight(z, prainhaShore(x), -32), 8, 4, 0.3f, Sand, false);
            if (dPrainha(p) <= 0 && sandHeight(z, prainhaShore(x), -32) < inside)
                inside = sandHeight(z, prainhaShore(x), -32), insideLook = Sand;
            cand(z + 60, -4.1f, 10, 6, 0.3f, Sand, false);  // mar abierto
            for (const Lane& l : lanes) {
                float lh = 0, d = laneDist(l, p, &lh);
                cand(d, lh, l.wall, l.run, l.slope, l.look, true);
            }
            for (const Area& a : areas) cand(a.dist(p), a.h, a.wall, a.run, a.slope, a.look, a.walk);
            if (inside < 1e8f) h = inside, look = insideLook;
            int dome = -1;
            // El fondo: morros que crecen lejos de las zonas, con la estatua en el más alto.
            auto bump = [&](Vec3 c, float amp, float r) {
                float q = length(flat(p - c));
                return amp * std::exp(-q * q / (2 * r * r));
            };
            float hills = bump(cerro, 70, 28) + bump({48, 0, 120}, 34, 30) + bump({-90, 0, 40}, 40, 40) + bump({72, 0, 10}, 16, 18) +
                          bump({-60, 0, 110}, 30, 30);
            hills += 4.0f * (fbm(x / 40.0f, z / 40.0f, 77) + 1.0f);
            h += hills * smoothstep01((dIn - 8) / 26);
            // El Pan de Azúcar y Urca: cúpulas de roca en la bahía.
            for (auto [c, amp, r] : {std::tuple<Vec3, float, float>{pao, 52.0f, 22.0f}, {urca, 22.0f, 16.0f}}) {
                Vec3 dq = flat(p - c);
                float q = std::sqrt(dq.x * dq.x + dq.z * dq.z * 0.64f);
                if (q < r) h = std::max(h, amp * std::pow(1.0f - (q / r) * (q / r), 0.55f)), dome = 1;
            }
            float qi = length(flat(p - kIslet));  // el islote del cocotero
            if (qi < 3.4f) h = std::max(h, -0.2f + 0.9f * (1 - (qi / 3.4f) * (qi / 3.4f)));
            float edge = std::min({x - t.x0, -t.x0 - x, -t.z0 - z});
            if (edge < 30) h += 30.0f * (1 - edge / 30) * (1 - edge / 30);
            size_t idx = size_t(iz) * size_t(t.nx) + size_t(ix);
            t.at(ix, iz) = h;
            t.fence[idx] = walk(p);
            zone[idx] = uint8_t(dome >= 0 ? Fuera : look);
        }
    // Color: arena, losas, adoquines, piedra, la cancha y la tierra de los becos; fuera, roca en
    // lo empinado y verde en lo demás.
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
                case Sand:
                    col = mixRgb(rgba(236, 216, 168), rgba(222, 200, 150), v), m = Material::Cloth;
                    if (h < 0.1f) col = mixRgb(rgba(190, 172, 132), col, clampf((h + 0.6f) / 0.7f, 0, 1));  // arena mojada
                    break;
                case Losas: col = mixRgb(rgba(214, 206, 190), rgba(190, 182, 168), cob); break;
                case Adoquines: col = mixRgb(rgba(150, 132, 112), rgba(118, 104, 90), cob); break;
                case PiedraClara: col = mixRgb(rgba(200, 186, 160), rgba(176, 162, 140), cob); break;
                case Cancha: {  // cemento verde con las líneas blancas
                    Vec3 q = Vec3{x, 0, z} - kQuadra;
                    bool line = std::fabs(std::fabs(q.x) - 8.4f) < 0.35f || std::fabs(std::fabs(q.z) - 5.4f) < 0.35f ||
                                std::fabs(q.x) < 0.3f || std::fabs(length(flat(q)) - 2.0f) < 0.3f;
                    col = line ? rgba(226, 224, 214) : mixRgb(rgba(62, 112, 86), rgba(70, 122, 92), v);
                    break;
                }
                case Tierra: col = mixRgb(rgba(140, 104, 76), rgba(120, 90, 66), cob), m = Material::Cloth; break;
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

    // Casas que no pisen las calles ni a otras casas.
    std::vector<Footprint> houses;
    auto fits = [&](const Footprint& f) {
        for (float sx : {-1.0f, 0.0f, 1.0f})
            for (float sz : {-1.0f, 0.0f, 1.0f})
                if (walk(f.corner(sx, sz)) < 0.25f) return false;
        for (const Footprint& o : houses)
            for (float sx : {-1.0f, 0.0f, 1.0f})
                for (float sz : {-1.0f, 0.0f, 1.0f})
                    if (o.contains(f.corner(sx, sz), 0.1f) || f.contains(o.corner(sx, sz), 0.1f)) return false;
        return true;
    };
    auto placeHouse = [&](const std::string& name, Footprint f) {
        if (!fits(f)) return false;
        houses.push_back(f);
        addProp(name, f.c, f.yaw);
        return true;
    };
    // Fachadas a lo largo de un borde: `edge(s)` da el punto del borde y la normal hacia fuera.
    // `kind` 0: sobrados; 1: casas de la favela.
    int sobradoN = 0, favelaN = 0;
    auto line = [&](auto edge, float len, int kind) {
        for (float s = 0; s < len; s += 0.6f) {
            auto [at, out] = edge(s);
            std::string name;
            float hw, hd;
            if (kind == 0) {
                int i = sobradoN % 8;
                name = "sobrado_" + std::to_string(i + 1), hw = (6.0f + float(i % 3) * 1.5f) * 0.5f, hd = 4.5f;
            } else {
                int i = favelaN % 8;
                name = "casa_favela_" + std::to_string(i + 1), hw = 2.8f, hd = 2.6f;
            }
            Footprint f{at + out * (hd + 0.35f), std::atan2(-out.x, -out.z), hw, hd};
            if (placeHouse(name, f)) {
                (kind == 0 ? sobradoN : favelaN) += 3;  // salta: que no se repitan seguidas
                s += hw * 2 - 0.6f;
            }
        }
    };
    auto lineLane = [&](const Lane& l, int kind, float from = 0, float to = 1e9f) {
        for (size_t i = 0; i + 1 < l.pts.size(); ++i) {
            const LanePoint &a = l.pts[i], &b = l.pts[i + 1];
            Vec3 pa{a.x, 0, a.z}, d = flat(Vec3{b.x, 0, b.z} - pa);
            float len = length(d);
            Vec3 dir = d * (1.0f / len), side{-dir.z, 0, dir.x};
            for (float sgn : {-1.0f, 1.0f})
                line(
                    [&](float s) {
                        float u = s / len, r = a.r + (b.r - a.r) * u;
                        return std::pair<Vec3, Vec3>{pa + dir * s + side * (sgn * r), side * sgn};
                    },
                    std::min(len, to) - std::max(0.0f, from), kind);
        }
    };
    auto lineArea = [&](const Area& a, int kind) {
        if (a.hz < 0) {
            float len = 2 * kPi * a.hx;
            line([&](float s) {
                float ang = s / a.hx;
                Vec3 n{std::cos(ang), 0, std::sin(ang)};
                return std::pair<Vec3, Vec3>{a.c + n * a.hx, n};
            }, len, kind);
            return;
        }
        const Vec3 n4[] = {{0, 0, 1}, {1, 0, 0}, {0, 0, -1}, {-1, 0, 0}};
        for (const Vec3& n : n4) {
            float half = n.x != 0 ? a.hz : a.hx;
            Vec3 along{-n.z, 0, n.x}, mid = a.c + Vec3{n.x * a.hx, 0, n.z * a.hz};
            line([&](float s) { return std::pair<Vec3, Vec3>{mid + along * (s - half), n}; }, half * 2, kind);
        }
    };
    // Faroles a un lado y a otro de la calle, cada pocos metros.
    auto lamps = [&](const Lane& l, float every, const char* prop) {
        float acc = every * 0.5f;
        int k = 0;
        for (size_t i = 0; i + 1 < l.pts.size(); ++i) {
            const LanePoint &a = l.pts[i], &b = l.pts[i + 1];
            Vec3 pa{a.x, 0, a.z}, d = flat(Vec3{b.x, 0, b.z} - pa);
            float len = length(d);
            Vec3 dir = d * (1.0f / len), side{-dir.z, 0, dir.x};
            for (; acc < len; acc += every, ++k) {
                float r = a.r + (b.r - a.r) * acc / len;
                addProp(prop, pa + dir * acc + side * ((k % 2 ? 1.0f : -1.0f) * (r - 0.45f)), std::atan2(dir.x, dir.z));
            }
            acc -= len;
        }
    };

    // --- La Caleta: playa salvaje -----------------------------------------------------------
    // El punto focal: un cocotero alto e inclinado en el islote, al que se llega vadeando.
    addProp("cocotero_alto", kIslet + Vec3{-0.6f, 0, 0.4f}, 2.6f, 1.05f);
    for (int i = 0; i < 5; ++i) {
        float a = float(i) * 1.3f + 0.4f;
        addProp(i % 2 ? "roca" : "piedra", kIslet + Vec3{std::cos(a) * 2.6f, 0, std::sin(a) * 2.6f}, a, rng.range(0.8f, 1.3f));
    }
    // Las peñas que cierran la playa: rocas grandes al pie y metidas en el agua (la del este
    // deja paso arriba a la Rua da Praia).
    for (float s : {-1.0f, 1.0f}) {
        for (float z = s > 0 ? -16.0f : -10.0f; z > -58; z -= 4.5f) {
            float x = s * (20.5f + rng.range(0, 2.5f) + std::max(0.0f, -z - 34) * 0.15f);
            addProp(rng.next() < 0.6f ? "roca_grande" : "roca", {x, 0, z}, rng.range(0, 6.28f), rng.range(1.0f, 1.8f));
        }
    }
    // Sin quiosco, ni sombrillas, ni socorrista: cocoteros inclinados, matorral de duna, troncos
    // varados y la selva que baja hasta la arena. Solo la barca del barquero.
    addProp("barca", {-7, 0, -31}, 1.2f);
    for (Vec3 p : {Vec3{-16.5f, 0, -11}, Vec3{16.5f, 0, -13}, Vec3{-17.5f, 0, -23}, Vec3{-13, 0, -19}, Vec3{12.5f, 0, -20},
                   Vec3{17, 0, -26}, Vec3{-6, 0, -12.5f}, Vec3{7, 0, -14}})
        addProp(int(p.x) % 2 ? "cocotero" : "cocotero_alto", p, rng.range(0, 6.28f), rng.range(0.9f, 1.15f));
    for (int i = 0; i < 26; ++i) {  // matorral y flores en lo alto de la arena, al pie del paseo
        Vec3 p{rng.range(-18, 18), 0, rng.range(-15, -9.5f)};
        float r = rng.next();
        addProp(r < 0.4f ? "hierba" : r < 0.65f ? "arbusto" : r < 0.85f ? "heliconia" : "platanera", p, rng.range(0, 6.28f),
                rng.range(0.7f, 1.1f));
    }
    for (Vec3 p : {Vec3{-11, 0, -22}, Vec3{4, 0, -19.5f}, Vec3{14, 0, -17}})  // troncos que trajo el mar
        addProp("tronco_caido", p, rng.range(0, 6.28f), rng.range(0.7f, 0.9f));
    for (int i = 0; i < 10; ++i) {  // piedras sueltas en la orilla
        Vec3 p{rng.range(-17, 17), 0, rng.range(-33, -24)};
        if (length(flat(p - Vec3{-7, 0, -31})) < 3) continue;
        addProp(i % 3 ? "piedra" : "roca", p, rng.range(0, 6.28f), rng.range(0.5f, 1.0f));
    }
    // El paseo de ondas entre la playa y la plaza, con dos farolas.
    for (float x : {-12.0f, 0.0f, 12.0f}) addProp("paseo_ondas", {x, kPlazaH, -5.5f}, 0, 1.0f, false);
    for (float x : {-8.0f, 8.0f}) addProp("farola_paseo", {x, 0, -8.2f}, 0);

    // --- El Largo de los Arcos -----------------------------------------------------------
    // Casas a los dos lados, de cara a la plaza (bajas: la plaza manda); hueco al oeste para
    // el callejón y al sureste para la Rua da Praia.
    auto house = [&](const char* name, float x, float z, float scl = 1.0f) {
        addProp(name, {x, 0, z}, x < 0 ? kPi * 0.5f : -kPi * 0.5f, scl);
        houses.push_back({{x, 0, z}, 0, 3.2f, 3.2f});
    };
    house("casa_favela_5", -15.6f, -3);
    house("casa_favela_3", -15.4f, 2);
    house("casa_favela_6", -15.6f, 7);
    house("casa_favela_1", -15.4f, 16.5f);
    house("edificio_playa_azul", 17, -2.4f, 0.34f);
    house("casa_favela_4", 15.6f, 3.4f);
    house("casa_favela_8", 15.4f, 8.4f);
    house("edificio_playa_rosa", 17, 13.5f, 0.3f);
    // El punto focal: los arcos, con el tranvía parado encima; por el arco central se pasa a
    // la Rua dos Arcos.
    const float kArc = 0.65f;
    addProp("arcos", {0, kPlazaH, 18.6f}, 0, kArc, false);
    houses.push_back({{0, 0, 18.6f}, 0, 11, 1.6f});
    addProp("tranvia_amarillo", {3.5f, kPlazaH + 15.2f * kArc, 18.6f}, kPi * 0.5f, kArc, false);
    addProp("almendro_playa", {-8.5f, 0, 12}, 0.5f);
    addProp("almendro_playa", {9, 0, 11.5f}, 2.1f);
    for (float x : {-4.5f, 4.5f}) addProp("banca_parque", {x, 0, 10.5f}, kPi);
    for (Vec3 p : {Vec3{-11, 0, -1}, Vec3{11, 0, -1}, Vec3{-11, 0, 15}, Vec3{11, 0, 15}}) addProp("farol_colonial", p);
    addProp("puesto_comida", {10.5f, 0, 5}, -kPi * 0.5f);

    // --- La Ladeira y el Mirador ---------------------------------------------------------
    // La escalera sube hacia el sur (hacia el mar), por detrás de las casas de la plaza.
    addProp("escalera_azulejos", {-23.5f, 3.0f, 3}, kPi, 1.0f, false);
    for (auto [name, p, yaw] : {std::tuple<const char*, Vec3, float>{"casa_favela_2", {-21, 0, 16.5f}, kPi},
                                {"casa_favela_8", {-28.5f, 0, 6}, kPi * 0.5f},
                                {"casa_favela_3", {-28.5f, 0, -1}, kPi * 0.5f},
                                {"casa_favela_7", {-30.5f, 0, -9}, kPi * 0.5f},
                                {"casa_favela_5", {-19.5f, 0, -8}, -kPi * 0.5f}}) {
        addProp(name, p, yaw);
        houses.push_back({p, 0, 3.0f, 3.0f});
    }
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

    // --- El centro: Rua dos Arcos, Praça do Coreto y sus calles ---------------------------
    addProp("kiosco", kCoreto + Vec3{0, 0, 2}, kPi);  // el quiosco de música, en medio
    addProp("chafariz", kCoreto + Vec3{-8, 0, -6});
    for (Vec3 p : {Vec3{9, 0, -6}, Vec3{-10, 0, 7}, Vec3{10, 0, 8}}) addProp("almendro_playa", kCoreto + p, rng.range(0, 6.28f), 1.1f);
    for (Vec3 p : {Vec3{-5, 0, -8}, Vec3{5, 0, 9}, Vec3{-12, 0, 0}}) addProp("banca_parque", kCoreto + p, std::atan2(-p.x, -p.z));
    for (Vec3 p : {Vec3{-14, 0, -11}, Vec3{14, 0, -11}, Vec3{-14, 0, 11}, Vec3{14, 0, 11}, Vec3{6, 0, -2}, Vec3{-6, 0, 6}})
        addProp("farol_colonial", kCoreto + p);
    addProp("puesto_comida", kCoreto + Vec3{12, 0, 2}, -kPi * 0.5f);
    // Sobrados: primero los de las plazas (las fachadas que mandan), luego las calles.
    lineArea(areas[2], 0);
    lineArea(areas[3], 0);
    lineArea(areas[4], 0);
    lineArea(areas[6], 0);
    for (int i : {1, 2, 3, 4, 5, 6, 9}) lineLane(lanes[size_t(i)], 0);
    for (int i : {1, 2, 3, 4, 5, 6, 9}) lamps(lanes[size_t(i)], 13, "farol_colonial");

    // --- Santa Teresa: el Largo da Igreja y el Largo dos Guimarães --------------------------
    addProp("iglesia", kIgreja + Vec3{0, 0, 20.5f}, kPi);  // de cara al largo y a la bahía
    houses.push_back({kIgreja + Vec3{0, 0, 20.5f}, 0, 6.5f, 11.5f});
    for (Vec3 p : {Vec3{-9, 0, -4}, Vec3{9, 0, -4}}) addProp("almendro_playa", kIgreja + p, rng.range(0, 6.28f), 1.2f);
    for (Vec3 p : {Vec3{-4, 0, -7}, Vec3{4, 0, -7}}) addProp("banca_parque", kIgreja + p, kPi);  // mirando a la ciudad
    for (Vec3 p : {Vec3{-12, 0, 6}, Vec3{12, 0, 6}, Vec3{-5, 0, -7.5f}}) addProp("farol_colonial", kIgreja + p);
    for (int i : {-3, -2, 2, 3}) addProp("valla", kIgreja + Vec3{float(i) * 3.6f, 0, -8.6f}, 0);  // el pretil, abierto a la cuesta
    addProp("pozo", kGuimaraes + Vec3{0, 0, 0.5f});
    for (Vec3 p : {Vec3{-5, 0, 4}, Vec3{5, 0, -4}}) addProp("farol_colonial", kGuimaraes + p);

    // --- El Morro: los becos de la favela y la cancha ---------------------------------------
    lineArea(areas[5], 1);
    lineLane(lanes[7], 1);
    lineLane(lanes[8], 1);
    lamps(lanes[7], 11, "farol_colonial");
    lamps(lanes[8], 11, "farol_colonial");
    for (float s : {-1.0f, 1.0f}) {  // las porterías de la cancha
        Vec3 g = kQuadra + Vec3{s * 8.6f, kQuadraH, 0};
        for (float z : {-1.5f, 1.5f}) addDecor(Mat4::identity(), g + Vec3{0, 1.0f, z}, {0.06f, 1.0f, 0.06f}, rgba(236, 234, 226), Material::Metal, false);
        addDecor(Mat4::identity(), g + Vec3{0, 2.0f, 0}, {0.06f, 0.06f, 1.56f}, rgba(236, 234, 226), Material::Metal, false);
    }
    for (Vec3 p : {Vec3{-9.5f, 0, -6.5f}, Vec3{9.5f, 0, 6.5f}}) addProp("farola_paseo", kQuadra + p, 0);

    // --- La Rua da Praia, el Largo do Bondinho y el teleférico ------------------------------
    for (int i = -3; i <= 3; ++i)  // el pretil del largo sobre la peña
        addProp("valla", kBondinho + Vec3{float(i) * 3.4f, 0, 9.4f}, 0);
    for (Vec3 p : {Vec3{-6, 0, 6}, Vec3{6, 0, 6}, Vec3{-8, 0, -4}}) addProp("farol_colonial", kBondinho + p);
    addProp("almendro_playa", kBondinho + Vec3{4, 0, 3}, 0.3f, 1.1f);
    addProp("banca_parque", kBondinho + Vec3{0.5f, 0, 2}, kPi * 0.3f);
    // Teleférico: de la estación del largo al morro de Urca y al Pan de Azúcar. Las tres
    // estaciones, de estuco y azulejo como la ciudad; las cabinas, pintadas como el tranvía.
    Vec3 station{stationAt.x, kBondinhoH, stationAt.z};
    float yawU = std::atan2(dirUrca.x, dirUrca.z);
    addProp("estacion_teleferico", station, yawU, 1.0f, false);
    houses.push_back({station, yawU, 5.0f, 5.4f});
    Vec3 topU = urca + Vec3{0, ground(urca.x, urca.z), 0}, topP = pao + Vec3{0, ground(pao.x, pao.z), 0};
    addProp("estacion_teleferico_morro", topU, std::atan2(station.x - topU.x, station.z - topU.z));
    addProp("estacion_teleferico_morro", topP, std::atan2(topU.x - topP.x, topU.z - topP.z));
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
    };
    cable(station, topU);
    cable(topU, topP);
    Vec3 mid1 = station + (topU - station) * 0.55f, mid2 = topU + (topP - topU) * 0.4f;
    Vec3 side1 = normalize(Vec3{topU.z - station.z, 0, station.x - topU.x}) * 1.6f, side2 = normalize(Vec3{topP.z - topU.z, 0, topU.x - topP.x}) * -1.6f;
    addProp("teleferico", mid1 + side1 + Vec3{0, 9 - 3.7f, 0}, std::atan2(topU.x - station.x, topU.z - station.z), 1.0f, false);
    addProp("teleferico", mid2 + side2 + Vec3{0, 9 - 3.7f, 0}, std::atan2(topP.x - topU.x, topP.z - topU.z), 1.0f, false);

    // --- La Prainha: la playa virgen, con su laguna --------------------------------------
    for (Vec3 p : {Vec3{76, 0, -36}, Vec3{80, 0, -33}, Vec3{86, 0, -35.5f}, Vec3{93, 0, -32}, Vec3{99, 0, -35}, Vec3{104, 0, -33},
                   Vec3{90, 0, -31}, Vec3{91, 0, -21}, Vec3{101, 0, -22}, Vec3{95, 0, -18.5f}, Vec3{89, 0, -26}})
        addProp(int(p.x) % 3 ? "cocotero_alto" : "cocotero", p, rng.range(0, 6.28f), rng.range(0.9f, 1.25f));
    for (int i = 0; i < 40; ++i) {
        Vec3 p{rng.range(72, 108), 0, rng.range(-32, -29)};
        float r = rng.next();
        addProp(r < 0.35f ? "hierba" : r < 0.6f ? "arbusto" : r < 0.8f ? "heliconia" : "platanera", p, rng.range(0, 6.28f),
                rng.range(0.7f, 1.2f));
    }
    for (Vec3 p : {Vec3{82, 0, -40}, Vec3{97, 0, -38.5f}}) addProp("tronco_caido", p, rng.range(0, 6.28f), 0.8f);
    for (float s : {-1.0f, 1.0f})  // las puntas de roca que cierran la playa
        for (int i = 0; i < 6; ++i) {
            Vec3 p{90 + s * (20 + rng.range(0, 3)), 0, -30 - float(i) * 4.0f};
            addProp(rng.next() < 0.6f ? "roca_grande" : "roca", p, rng.range(0, 6.28f), rng.range(1.0f, 1.9f));
        }
    for (int i = 0; i < 8; ++i) {  // piedras de la orilla
        Vec3 p{rng.range(74, 106), 0, rng.range(-46, -41)};
        addProp(i % 3 ? "piedra" : "roca", p, rng.range(0, 6.28f), rng.range(0.5f, 1.1f));
    }
    lamps(lanes[10], 12, "farol_camino");

    // --- El telón de fondo (no se pisa) ---------------------------------------------------
    // La favela trepando por el morro del oeste y por detrás de Santa Teresa.
    for (int i = 0; i < 520; ++i) {
        Vec3 p{rng.range(-125, 10), 0, rng.range(-30, 135)};
        if (p.x > -40 && p.z < 100) continue;  // el centro: sobrados, no favela
        if (walk(p) < 7 || length(flat(p - cerro)) < 34) continue;
        float g = ground(p.x, p.z);
        if (g < 4) continue;
        Vec3 n = t.normalAt(p.x, p.z);  // de cara a la bahía, hacia abajo de la ladera
        float yaw = std::round(std::atan2(n.x, n.z) / (kPi * 0.5f)) * kPi * 0.5f + rng.range(-0.08f, 0.08f);
        Footprint f{p, yaw, 2.8f, 2.6f};
        if (!fits(f)) continue;
        houses.push_back(f);
        addProp("casa_favela_" + std::to_string(1 + rng.pick(8)), p, yaw);
    }
    addProp("estatua_cerro", {cerro.x, 0, cerro.z}, kPi);
    // Selva en las laderas que rodean las zonas.
    for (int i = 0; i < 650; ++i) {
        Vec3 p{rng.range(-160, 160), 0, rng.range(-70, 160)};
        float g = ground(p.x, p.z);
        if (walk(p) < 3.5f || g < 1.5f) continue;
        bool taken = false;
        for (const Footprint& f : houses) taken |= f.contains(p, 1.0f);
        if (taken) continue;
        float r = rng.next();
        addProp(r < 0.45f ? "almendro_playa" : r < 0.7f ? "platanera" : r < 0.88f ? "cocotero" : "guadual", p, rng.range(0, 6.28f),
                rng.range(0.8f, 1.3f));
    }

    // --- Habitantes, portales y nodos ----------------------------------------------------
    addNpc("barquero", {-5, 0, -28}, 4);
    addNpc("oficinista", {3, 0, 6}, 4);
    addNpc("pregonero", {-6, 0, 2}, 3);
    addNpc("musica", {2.0f, 0, 16.2f}, 1);  // toca bajo los arcos
    addNpc("trapera", {-20, 0, 10.5f}, 2);
    addNpc("relojero", kMirador + Vec3{1, 0, -2}, 2);
    addNpc("abuelo", kCoreto + Vec3{-4, 0, -7}, 3);
    addNpc("vendedora", kCoreto + Vec3{10.5f, 0, 2}, 2);
    addNpc("vidriera", {-21, 0, 76}, 4);
    addNpc("monja_fanal", kIgreja + Vec3{0, 0, 6}, 3);
    addNpc("nino_sombras", kQuadra + Vec3{2, 0, 1}, 5);
    addNpc("deshollinadora", {-56, 0, 26}, 3);
    addNpc("guardia_apagado", kBondinho + Vec3{-3, 0, -3}, 2);
    addNpc("surfista", {92, 0, -40}, 6);
    heroStart_ = {0, kPlazaH, -4};
    heroStartYaw_ = 0;  // de cara a la plaza y a los arcos; la caleta, a la espalda
    addPortal({7.5f, kPlazaH, 2});
    addPortal(kMirador + Vec3{0.5f, kMiradorH, 3});
    addPortal(kCoreto + Vec3{-9, kCoretoH, 4});
    addPortal(kIgreja + Vec3{8, kIgrejaH, 2});
    // Nodos de color: el islote, la praça, la iglesia, la cancha, el bondinho y la Prainha.
    const Vec3 nodeSpots[] = {kIslet + Vec3{1.6f, 0, 1.4f}, kCoreto + Vec3{6, 0, -7}, kIgreja + Vec3{-6, 0, 2},
                              kQuadra + Vec3{-6, 0, -3},     kBondinho + Vec3{-2, 0, 6}, {100, 0, -37}};
    const uint32_t nodeColors[] = {rgba(255, 190, 60), rgba(40, 220, 255), rgba(255, 70, 190),
                                   rgba(120, 255, 140), rgba(255, 120, 60), rgba(150, 120, 255)};
    for (int k = 0; k < 6; ++k) {
        Vec3 p = nodeSpots[k];
        p.y = ground(p.x, p.z);
        nodes_.push_back({"nodo_bahia_" + std::to_string(k), p, 24, nodeColors[k], ""});
    }
    LevelPart explore{"Ciudad de la Bahía", "Enciende los seis nodos de la bahía", PartKind::Nodes, "", 6, {}, nodeSpots[0], {}, "", ""};
    parts_ = {explore};
    nextLevel_ = LevelId::Rio;
}
