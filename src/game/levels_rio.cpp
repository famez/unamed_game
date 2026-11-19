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
//   - El teleférico (F en el andén) sube al Morro da Urca y de allí al Pão de Açúcar: en lo alto
//     de cada uno, una explanada con su estación y su mirador.
// En las plazas, tiendas (el armazém, la botica, el amolador: F a quien atiende para comprar)
// y tabernas (F al tabernero: se descansa, con la vida llena y punto de control).
// El recinto (Terrain::fence) sigue el borde de las zonas; las casas y las laderas lo tapan.

#include "game/game.h"
#include "game/map_tools.h"
#include "game/props.h"

#include <cstdio>
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
// El Morro da Urca y el Pão de Açúcar (cúpulas de roca en la bahía) con su explanada arriba.
const Vec3 kUrca{18, 0, -92}, kPao{50, 0, -112};
const float kUrcaTop = 22.0f, kPaoTop = 52.0f, kTopR = 10.0f;

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
    const Vec3 dirUrca = normalize(flat(kUrca - kBondinho));
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
        {kUrca, kTopR, -1, 0, kUrcaTop, PiedraClara, false},  // las explanadas del teleférico (se pisan en su zona)
        {kPao, kTopR, -1, 0, kPaoTop, PiedraClara, false},
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
    const Vec3 pao = kPao, urca = kUrca, cerro{-15, 0, 145};

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
                float hd = q < r ? amp * std::pow(1.0f - (q / r) * (q / r), 0.55f) : -1e9f;
                if (hd > h + 0.01f) h = hd, dome = 1;  // la explanada de arriba se queda como está
            }
            for (auto [c, top] : {std::pair<Vec3, float>{kUrca, kUrcaTop}, {kPao, kPaoTop}})  // la explanada, algo más ancha
                if (length(flat(p - c)) < kTopR + 1.5f) h = std::max(h, top);                // que lo que se anda
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
    seaReach_ = 500;  // al sur, la bahía se abre al mar: que llegue al horizonte (se ve desde los morros)
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
    // Tiendas y tabernas (planta baja de un sobrado de 3,5 m de fondo): la fachada da a la calle
    // en `at` (el borde de lo que se anda), con `out` hacia fuera.
    auto shopfront = [&](const char* name, Vec3 at, Vec3 out, float hw) {
        bool ok = placeHouse(name, {at + out * (3.5f + 0.35f), std::atan2(-out.x, -out.z), hw, 3.5f});
        if (!ok) std::fprintf(stderr, "bahía: no cabe %s en (%.1f, %.1f)\n", name, double(at.x), double(at.z));
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
    // Al este, el armazém y el botequim (ver abajo, con quien atiende) y una casa entre ellos.
    shopfront("tienda_armazem", {13, 0, -1}, {1, 0, 0}, 3.0f);
    house("casa_favela_4", 15.6f, 5.2f);
    shopfront("taberna_arco", {13, 0, 12.2f}, {1, 0, 0}, 3.5f);
    // El punto focal: los arcos, con el tranvía parado encima; por el arco central se pasa a
    // la Rua dos Arcos.
    const float kArc = 0.65f;
    addProp("arcos", {0, kPlazaH, 18.6f}, 0, kArc, false);
    houses.push_back({{0, 0, 18.6f}, 0, 11, 1.6f});
    addProp("tranvia_amarillo", {3.5f, kPlazaH + 15.2f * kArc, 18.6f}, kPi * 0.5f, kArc, false);
    addProp("almendro_playa", {-8.5f, 0, 12}, 0.5f);
    addProp("almendro_playa", {5.0f, 0, 14.0f}, 2.1f);
    for (float x : {-4.5f, 4.5f}) addProp("banca_parque", {x, 0, 10.5f}, kPi);
    for (Vec3 p : {Vec3{-11, 0, -1}, Vec3{10, 0, 4}, Vec3{-11, 0, 15}}) addProp("farol_colonial", p);
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
    // La botica al norte, el amolador al oeste y la taberna da Lapa al este; luego los sobrados.
    shopfront("tienda_botica", kCoreto + Vec3{6, 0, 12}, {0, 0, 1}, 3.0f);
    shopfront("tienda_amolador", kCoreto + Vec3{-15, 0, 6}, {-1, 0, 0}, 3.0f);
    shopfront("taberna_lapa", kCoreto + Vec3{15, 0, 6}, {1, 0, 0}, 3.5f);
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
    // Teleférico: de la estación del largo al Morro da Urca y de allí al Pão de Açúcar. Las tres
    // estaciones, de estuco y azulejo como la ciudad; las cabinas, pintadas como el tranvía (van
    // y vienen: Game::cableLines). Arriba, cada estación mira a la de abajo y tiene detrás su
    // terraza, en la explanada del morro.
    Vec3 station{stationAt.x, kBondinhoH, stationAt.z};
    float yawU = std::atan2(dirUrca.x, dirUrca.z);
    addProp("estacion_teleferico", station, yawU, 1.0f, false);
    houses.push_back({station, yawU, 5.0f, 5.4f});
    const Vec3 dirPao = normalize(flat(kPao - kUrca));
    Vec3 topU = kUrca - dirUrca * 4.0f + Vec3{0, kUrcaTop, 0}, topP = kPao - dirPao * 4.0f + Vec3{0, kPaoTop, 0};
    addProp("estacion_teleferico_morro", topU, std::atan2(-dirUrca.x, -dirUrca.z), 1.0f, false);
    addProp("estacion_teleferico_morro", topP, std::atan2(-dirPao.x, -dirPao.z), 1.0f, false);
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
    // Las líneas: el cable a 9 m de cada estación y los andenes junto a sus puertas (la de abajo
    // da al largo; las de arriba, a la terraza: allí, una a cada lado, la de bajar y la de subir).
    auto cableLine = [&](Vec3 a, Vec3 b, Vec3 stopA, Vec3 stopB, const char* nameA, const char* nameB) {
        CableLine l;
        l.a = a + Vec3{0, 9, 0};
        l.b = b + Vec3{0, 9, 0};
        l.side = normalize(Vec3{b.z - a.z, 0, a.x - b.x}) * 1.6f;
        l.stopA = {stopA.x, ground(stopA.x, stopA.z), stopA.z};
        l.stopB = {stopB.x, ground(stopB.x, stopB.z), stopB.z};
        Vec3 fa = flat(stopA - a), fb = flat(stopB - b);  // al bajar, de espaldas a la estación
        l.yawA = std::atan2(fa.x, fa.z);
        l.yawB = std::atan2(fb.x, fb.z);
        l.nameA = nameA;
        l.nameB = nameB;
        cables_.push_back(l);
    };
    // (Fuera de la caja de choque de la estación, que al ir girada es más grande que ella.)
    Vec3 sideU{-dirUrca.z, 0, dirUrca.x};
    cableLine(station, topU, station - dirUrca * 8.0f, topU + dirUrca * 9.0f + sideU * 2.2f, "Largo do Bondinho", "Morro da Urca");
    cables_.back().toZone = LevelId::Urca;  // arriba es otra zona (levels_cimas.cpp): se carga al llegar
    cables_.back().toArrival = arrival::kUrcaFromBay;
    (void)topP;
    // Las cimas (Urca y el Pão de Açúcar) son otras zonas: desde aquí solo se ven sus morros con
    // las estaciones arriba (buildSummit, al final).

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
    // Quien atiende las tiendas (entre el mostrador y la puerta). En las tabernas se entra: son
    // otras zonas (levels_interiores.cpp), con su tabernero dentro.
    auto keeper = [&](const char* model, Vec3 at, Vec3 out, float inFront, const char* name, NpcRole role,
                      std::vector<std::string> lines, std::vector<ShopItem> stock = {}) {
        addNpc(model, at - out * inFront, 0.4f, name, role, std::move(lines));
        npcs_.back().stock = std::move(stock);
        npcs_.back().body.yaw = std::atan2(-out.x, -out.z);
    };
    keeper("aceitero", {13, 0, -1}, {1, 0, 0}, 0.1f, "Seu Candido, armazém", NpcRole::Shop,
           {"Velas, cerillas, células. Lo que alumbra se vende aquí.", "Desde que se paró la hora no he cerrado. Tampoco ha venido nadie."},
           {{ItemId::Potion, 25}, {ItemId::HiPotion, 70}, {ItemId::Ether, 45}});
    keeper("recolectora", kCoreto + Vec3{6, 0, 12}, {0, 0, 1}, 0.1f, "Dona Íris, botica", NpcRole::Shop,
           {"Hierbas del morro y luz embotellada. No mezcles las dos.", "Cada frasco lo llené antes de que se parara el sol. No quedan muchos."},
           {{ItemId::HiPotion, 65}, {ItemId::Ether, 40}, {ItemId::Elixir, 220}});
    keeper("oficinista", kCoreto + Vec3{-15, 0, 6}, {-1, 0, 0}, 0.1f, "Amolador", NpcRole::Shop,
           {"Una hoja de luz también se mella. Se nota en el sonido.", "Piezas de Grises, las compro y las vendo. No pregunto de dónde salen."},
           {{ItemId::Sharpener, 160}, {ItemId::ShinyShard, 30}, {ItemId::WingDust, 35}, {ItemId::GoldSpore, 40}});
    heroStart_ = {0, kPlazaH, -4};
    heroStartYaw_ = 0;  // de cara a la plaza y a los arcos; la caleta, a la espalda
    // Las llegadas (arrival::kRio…): el principio, la salida de cada taberna y el andén del
    // teleférico (al volver de Urca). Las puertas de las tabernas, en la acera de su fachada.
    arrivals_.push_back({heroStart_, heroStartYaw_});
    for (auto [at, out, to, label] : {std::tuple<Vec3, Vec3, LevelId, const char*>{{13, 0, 12.2f}, {1, 0, 0}, LevelId::Botequim, "Entrar · Botequim"},
                                      {kCoreto + Vec3{15, 0, 6}, {1, 0, 0}, LevelId::TabernaLapa, "Entrar · Taberna da Lapa"}}) {
        Vec3 door = at - out * 0.6f, outside = at - out * 1.6f;
        door.y = ground(door.x, door.z);
        doors_.push_back({door, to, arrival::kInside, label});
        arrivals_.push_back({{outside.x, ground(outside.x, outside.z), outside.z}, std::atan2(-out.x, -out.z)});
    }
    arrivals_.push_back({cables_[0].stopA, cables_[0].yawA});
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

// --- Las cimas: el Morro da Urca y el Pão de Açúcar ----------------------------------------
// Cada una es su propia zona, a la que se llega en el teleférico. Se construye la bahía entera
// como telón de fondo (el relieve, el mar y lo grande: casas, estaciones, los arcos, la estatua)
// y se quita todo lo que se juega o se ve de cerca (habitantes, farolas, nodos, lo pequeño: sus
// piezas se sueltan). El morro se rehace más grande, con lo que se pisa: la explanada de la
// estación, senderos que bajan por la ladera y lo que hay al final de cada uno.
//   - Urca: la explanada (el puesto de Dona Cida), el mirante sobre la ciudad y, abajo, las
//     ruinas de la Fortaleza tomadas por los Grises (y su cápsula).
//   - Pão de Açúcar: la explanada (el guarda del cume), el Mirante do Céu y la Agulha, una
//     aguja de roca a la que se sube por una escalera de gato: arriba, la cápsula de la cima.

void Game::buildUrca() { buildSummit(false); }
void Game::buildPaoAcucar() { buildSummit(true); }

void Game::buildSummit(bool pao) {
    buildRio();
    levelName_ = pao ? "Pan de Azúcar" : "Morro da Urca";
    npcs_.clear();
    nodes_.clear();
    parts_.clear();
    chests_.clear();
    portals_.clear();
    doors_.clear();
    arrivals_.clear();
    cables_.clear();
    lamps_.clear();
    interacts_.clear();
    encounters_.clear();
    lanterns_.clear();
    wildSpawns_ = false;
    const Vec3 c = pao ? kPao : kUrca;
    const float top = pao ? kPaoTop : kUrcaTop;
    {  // De la bahía, solo lo grande y lejos del morro; lo demás se suelta al acabar (endZone).
        PropLibrary& lib = propLibrary();
        std::vector<PropInstance> keep;
        for (const PropInstance& p : props_)
            if (p.radius >= 3.0f && length(flat(p.center - c)) > 14.0f) keep.push_back(p);
        props_ = std::move(keep);
        lib.beginZone();
        for (const PropInstance& p : props_) lib[size_t(p.def)].used = true;
    }
    Terrain& t = const_cast<Terrain&>(*world_.terrain);
    auto ground = [&](float x, float z) { return t.heightAt(x, z); };
    const Vec3 dirUrca = normalize(flat(kUrca - kBondinho)), dirPao = normalize(flat(kPao - kUrca));
    const Vec3 u = pao ? dirPao : dirUrca, v{-u.z, 0, u.x};
    // Un punto alrededor del centro: `deg` grados desde `u` (180: hacia la estación) a `q` m.
    auto polar = [&](float deg, float q) {
        float a = deg * kPi / 180.0f;
        return c + u * (std::cos(a) * q) + v * (std::sin(a) * q);
    };
    auto lp = [&](float deg, float q, float h, float r) {
        Vec3 p = polar(deg, q);
        return LanePoint{p.x, p.z, h, r};
    };
    // Lo que se pisa: la explanada, los miradores y el final de cada sendero; los senderos.
    std::vector<Area> areas;
    std::vector<Lane> trails;
    Vec3 mirante, fin;
    if (!pao) {
        mirante = polar(-95, 16);
        fin = polar(15, 18);
        areas = {{c, kTopR, -1, 0, top, PiedraClara}, {mirante, 4.5f, -1, 0, 16, PiedraClara}, {fin, 5.5f, -1, 0, 9.5f, Fuera}};
        trails = {{{lp(-60, 9.0f, top, 1.4f), lp(-80, 13, 19, 1.3f), lp(-95, 15, 16.2f, 1.3f)}, Tierra},
                  {{lp(-95, 17, 16, 1.3f), lp(-50, 18.5f, 13, 1.3f), lp(-15, 19.5f, 10.5f, 1.3f), lp(15, 17, 9.5f, 1.4f)}, Tierra}};
    } else {
        mirante = polar(120, 16);
        fin = polar(40, 19);
        areas = {{c, kTopR, -1, 0, top, PiedraClara}, {mirante, 4.5f, -1, 0, 46, PiedraClara}, {fin, 5.0f, -1, 0, 41, PiedraClara}};
        trails = {{{lp(70, 9.0f, top, 1.4f), lp(95, 13, 49.5f, 1.3f), lp(118, 14.5f, 46.3f, 1.3f)}, Tierra},
                  {{lp(118, 17.5f, 46, 1.3f), lp(85, 19.5f, 43.5f, 1.3f), lp(55, 19.5f, 41.3f, 1.3f), lp(40, 18, 41, 1.4f)}, Tierra}};
    }
    auto walk = [&](Vec3 p) {
        float d = 1e9f;
        for (const Area& a : areas) d = std::min(d, a.dist(p));
        for (const Lane& l : trails) d = std::min(d, laneDist(l, p, nullptr));
        return d;
    };
    // El morro, más ancho que el que se ve desde la bahía, con lo que se pisa tallado en él.
    const float amp = pao ? 56.0f : 26.0f, radius = pao ? 30.0f : 27.0f;
    for (int iz = 0; iz < t.nz; ++iz)
        for (int ix = 0; ix < t.nx; ++ix) {
            float x = t.x0 + float(ix) * t.cell, z = t.z0 + float(iz) * t.cell;
            Vec3 p{x, 0, z};
            size_t idx = size_t(iz) * size_t(t.nx) + size_t(ix);
            t.fence[idx] = walk(p);
            float q = length(flat(p - c));
            if (q > radius) continue;
            float dome = amp * std::pow(1.0f - (q / radius) * (q / radius), 0.55f) + 1.2f * fbm(x / 6.0f, z / 6.0f, 31);
            float h = dome, inside = 1e9f;
            Look look = Fuera;
            auto cand = [&](float d, float base, Look lk) {
                if (d <= 0 && base < inside) inside = base, look = lk;
                h = std::min(h, base + rise(d, 5, 2.5f, 0.6f));
            };
            for (const Area& a : areas) cand(a.dist(p), a.h, a.look);
            for (const Lane& l : trails) {
                float lh = 0, d = laneDist(l, p, &lh);
                cand(d, lh, l.look);
            }
            bool walkable = inside < 1e8f;
            if (walkable) h = inside;
            h = std::max(h, std::min(t.at(ix, iz), 0.0f));  // el fondo del mar, al pie
            t.at(ix, iz) = h;
            float n = noise(x / 5.0f, z / 5.0f, 21) * 0.5f + 0.5f, cob = noise(x * 1.4f, z * 1.4f, 4) * 0.5f + 0.5f;
            uint32_t col;
            Material m = Material::Stone;
            if (walkable && look == PiedraClara) col = mixRgb(rgba(200, 186, 160), rgba(176, 162, 140), cob);
            else if (walkable && look == Tierra) col = mixRgb(rgba(140, 104, 76), rgba(120, 90, 66), cob), m = Material::Cloth;
            else if (walkable) col = mixRgb(rgba(70, 120, 58), rgba(96, 140, 70), n), m = Material::Grass;  // el claro
            else {
                col = mixRgb(rgba(48, 104, 52), rgba(78, 138, 62), n), m = Material::Grass;
                if (h > 1 && q > radius * 0.55f && n > 0.35f) col = mixRgb(rgba(118, 108, 98), rgba(150, 140, 126), n), m = Material::Stone;  // la roca pelada
            }
            paintTerrain(ix, iz, col, m);
        }
    ++terrainVersion_;
    auto at = [&](Vec3 p) { return Vec3{p.x, ground(p.x, p.z), p.z}; };
    auto yawTo = [](Vec3 from, Vec3 to) {
        Vec3 d = flat(to - from);
        return std::atan2(d.x, d.z);
    };

    // La estación de esta cima (la de la otra y la de abajo ya están: son de la bahía).
    const Vec3 station{kBondinho.x + dirUrca.x * 15.5f, kBondinhoH, kBondinho.z + dirUrca.z * 15.5f};
    const Vec3 topU = kUrca - dirUrca * 4.0f + Vec3{0, kUrcaTop, 0}, topP = kPao - dirPao * 4.0f + Vec3{0, kPaoTop, 0};
    const Vec3 sideU{-dirUrca.z, 0, dirUrca.x};
    addProp("estacion_teleferico_morro", pao ? topP : topU, pao ? std::atan2(-dirPao.x, -dirPao.z) : std::atan2(-dirUrca.x, -dirUrca.z), 1.0f,
            false);
    auto line = [&](Vec3 a, Vec3 b, Vec3 stop, const char* nameA, const char* nameB, LevelId to, int arrival) {
        CableLine l;
        l.a = a + Vec3{0, 9, 0};
        l.b = b + Vec3{0, 9, 0};
        l.side = normalize(Vec3{b.z - a.z, 0, a.x - b.x}) * 1.6f;
        l.stopA = at(stop);
        l.stopB = at(b);
        l.yawA = yawTo(a, stop);
        l.nameA = nameA;
        l.nameB = nameB;
        l.toZone = to;
        l.toArrival = arrival;
        cables_.push_back(l);
        arrivals_.push_back({l.stopA, l.yawA});  // y quien llega por esta línea, aparece en su andén
    };
    if (!pao) {
        line(topU, station, topU + dirUrca * 9.0f + sideU * 2.2f, "Morro da Urca", "Largo do Bondinho", LevelId::Rio, arrival::kRioCable);
        line(topU, topP, topU + dirUrca * 9.0f - sideU * 2.2f, "Morro da Urca", "Pan de Azúcar", LevelId::PaoAcucar, arrival::kPaoFromUrca);
    } else {
        line(topP, topU, topP + dirPao * 9.0f, "Pan de Azúcar", "Morro da Urca", LevelId::Urca, arrival::kUrcaFromPao);
    }
    heroStart_ = arrivals_[0].pos;
    heroStartYaw_ = arrivals_[0].yaw;

    Rng rng{pao ? 5203u : 5101u};
    // Pretil de hierro al borde de una explanada, abierto hacia `gaps` (direcciones desde su centro).
    auto railing = [&](Vec3 ctr, float r, std::vector<Vec3> gaps, float gap = 0.55f) {
        int n = int(2 * kPi * r / 2.6f);
        for (int i = 0; i < n; ++i) {
            float a = float(i) / float(n) * 2 * kPi;
            Vec3 dn{std::cos(a), 0, std::sin(a)};
            bool open = false;
            for (Vec3 g : gaps) open = open || dot(dn, normalize(flat(g))) > gap;
            if (!open) addProp("valla", ctr + dn * (r - 0.5f), -a - kPi * 0.5f, 1.0f);
        }
    };
    auto faroles = [&](const Lane& l, float every) {
        float acc = every * 0.5f;
        for (size_t i = 0; i + 1 < l.pts.size(); ++i) {
            Vec3 a{l.pts[i].x, 0, l.pts[i].z}, b{l.pts[i + 1].x, 0, l.pts[i + 1].z};
            float len = length(b - a);
            Vec3 d = (b - a) * (1.0f / len), side{-d.z, 0, d.x};
            for (; acc < len; acc += every) addProp("farol_camino", a + d * acc + side * (l.pts[i].r + 0.4f), 0);
            acc -= len;
        }
    };
    // La ladera: selva de almendros, cocoteros y matas donde no se pisa (ni hay roca pelada).
    for (int i = 0; i < (pao ? 70 : 60); ++i) {
        float a = rng.range(0, 2 * kPi), q = rng.range(6, radius * 0.92f);
        Vec3 p = c + Vec3{std::cos(a) * q, 0, std::sin(a) * q};
        float g = ground(p.x, p.z);
        if (walk(p) < 1.6f || g < 1.0f || 1.0f - t.normalAt(p.x, p.z).y > 0.45f) continue;
        int k = rng.next() < 0.4f ? 0 : rng.next() < 0.5f ? 1 : 2;
        addProp(k == 0 ? "almendro_playa" : k == 1 ? (rng.next() < 0.5f ? "arbusto_flores" : "arbusto") : "cocotero", p, rng.range(0, 6.28f),
                rng.range(0.8f, 1.15f));
    }
    // Lo que hay en la explanada y en el mirante.
    Vec3 toStation = -u, toTrail = trails[0].pts.size() ? Vec3{trails[0].pts[0].x, 0, trails[0].pts[0].z} - c : u;
    railing(c, kTopR, {toStation, toTrail});
    Vec3 out = u, across = v;
    for (float s : {-1.0f, 1.0f}) {
        addProp("banca_parque", c + out * 6.5f + across * (s * 3.5f), std::atan2(out.x, out.z));
        addProp("farol_colonial", c + across * (s * 7.5f) + out * 1.0f);
    }
    addProp("almendro_playa", c - across * 6.0f + out * 3.0f, rng.range(0, 6.28f), 0.9f);
    Vec3 toMirante = flat(mirante - c);
    railing(mirante, 4.5f, {-toMirante + (Vec3{trails[0].pts.back().x, 0, trails[0].pts.back().z} - mirante) * 0.2f}, 0.35f);
    addProp("banca_parque", mirante + normalize(toMirante) * 1.8f + normalize(Vec3{-toMirante.z, 0, toMirante.x}) * 1.4f,
            std::atan2(toMirante.x, toMirante.z));
    addProp("farol_colonial", mirante - normalize(Vec3{-toMirante.z, 0, toMirante.x}) * 2.6f);
    {  // un catalejo de monedas, de cara a la ciudad
        Vec3 g = at(mirante + normalize(toMirante) * 2.6f - normalize(Vec3{-toMirante.z, 0, toMirante.x}) * 1.2f);
        Mat4 id = Mat4::identity();
        addDecor(id, g + Vec3{0, 0.55f, 0}, {0.06f, 0.55f, 0.06f}, rgba(60, 64, 70), Material::Metal, false);
        Mat4 m = translate(g + Vec3{0, 1.2f, 0}) * rotateY(std::atan2(toMirante.x, toMirante.z));
        addDecor(m, {0, 0, 0.1f}, {0.09f, 0.09f, 0.3f}, rgba(196, 160, 72), Material::Metal, false);
        propSolids_.push_back({g - Vec3{0.2f, 0, 0.2f}, g + Vec3{0.2f, 1.3f, 0.2f}});
    }
    for (const Lane& l : trails) faroles(l, 7.0f);

    if (!pao) {
        // Dona Cida y su puesto de agua de coco, en la explanada.
        Vec3 kiosk = c + across * 4.0f - out * 1.5f;
        addProp("quiosco_playa", kiosk, std::atan2(-across.x, -across.z));
        addNpc("vendedora", kiosk - across * 2.0f, 0.4f, "Dona Cida, agua de coco", NpcRole::Shop,
               {"Coco frío del morro. Bueno, frío no: del tiempo que hacía.", "Abajo, en la Fortaleza, se han metido los Grises. Ya nadie baja."});
        npcs_.back().stock = {{ItemId::Potion, 22}, {ItemId::Ether, 40}, {ItemId::HiPotion, 65}};
        npcs_.back().body.teleport(npcs_.back().body.body.pos, std::atan2(-across.x, -across.z) + kPi);
        addNpc("surfista", mirante + Vec3{0.5f, 0, 0.5f}, 1.5f, "Bruno", NpcRole::Townsfolk,
               {"Desde aquí se ve la ola parada en la Prainha. Lleva así desde que se paró la hora.",
                "La bajada a la Fortaleza es por ese sendero. Yo de ti no iría sola."});
        // La Fortaleza: muros rotos de piedra, dos cañones y su cápsula, tomada por los Grises.
        Vec3 fu = normalize(flat(fin - c)), fv{-fu.z, 0, fu.x};
        float fy = 9.5f;
        uint32_t stone = rgba(150, 140, 126);
        for (auto [o, len, hgt] : {std::tuple<float, float, float>{-1, 3.5f, 1.8f}, {1, 2.5f, 1.2f}}) {
            Vec3 wc = fin + fu * 4.6f + fv * (o * 2.6f);
            addBlock({wc.x, fy + hgt * 0.5f, wc.z}, {std::fabs(fv.x) * len * 0.5f + 0.35f, hgt * 0.5f, std::fabs(fv.z) * len * 0.5f + 0.35f}, stone);
        }
        for (float s : {-1.0f, 1.0f}) {
            Vec3 wc = fin + fv * (s * 4.4f) + fu * 1.0f;
            addBlock({wc.x, fy + 0.8f, wc.z}, {std::fabs(fu.x) * 1.4f + 0.35f, 0.8f, std::fabs(fu.z) * 1.4f + 0.35f}, stone);
            Vec3 cn = fin + fu * 3.4f + fv * (s * 1.6f);
            Mat4 m = translate({cn.x, fy + 0.55f, cn.z}) * rotateY(std::atan2(fu.x, fu.z));
            addDecor(m, {0, 0, 0.2f}, {0.16f, 0.16f, 0.75f}, rgba(46, 46, 50), Material::Metal, false);
            addDecor(m, {0, -0.3f, 0}, {0.3f, 0.2f, 0.4f}, rgba(96, 66, 44), Material::Cloth, false);
        }
        TreasureChest chest;
        chest.pos = at(fin + fu * 2.2f);
        chest.yaw = std::atan2(-fu.x, -fu.z);
        chest.contents = {{ItemId::HiPotion, 2}, {ItemId::ShinyShard, 3}};
        chest.munny = 80;
        chests_.push_back(chest);
        Vec3 e1 = at(fin + fv * 2.5f), e2 = at(fin - fv * 2.5f), e3 = at(fin + fu * 1.5f), e4 = at(fin - fu * 2.0f);
        encounters_ = {{"urca_fortaleza", AABB{fin - Vec3{6, 2, 6} + Vec3{0, fy, 0}, fin + Vec3{6, 5, 6} + Vec3{0, fy, 0}},
                        {{{EnemyKind::Hound, e1}, {EnemyKind::Hound, e2}, {EnemyKind::Dazzled, e3}},
                         {{EnemyKind::Brute, e3}, {EnemyKind::Watcher, e4}, {EnemyKind::Hound, e1}}},
                        {}, "", ""}};
        LevelPart part;
        part.title = "Morro da Urca";
        part.objective = "Echa a los Grises de la Fortaleza";
        part.kind = PartKind::Encounter;
        part.target = "urca_fortaleza";
        part.marker = at(fin) + Vec3{0, 1.5f, 0};
        parts_ = {part};
    } else {
        addNpc("relojero", c + across * 3.0f + out * 2.0f, 1.0f, "El guarda del cume", NpcRole::Townsfolk,
               {"Subí a dar cuerda al faro de la cima. Desde entonces es la misma hora aquí arriba que abajo.",
                "La Agulha, la aguja de piedra de la ladera: arriba quedó la caja del guarda. Se sube por la escalera."});
        // La Agulha: una aguja de roca en el saliente, con la escalera de gato por la cara que da
        // al sendero y la cápsula de la cima arriba. Los Grises guardan el saliente.
        Vec3 fu = normalize(flat(fin - c)), fv{-fu.z, 0, fu.x};
        const float base = 41, spire = 47.5f;
        Vec3 sp = fin + fu * 1.6f;
        addBlock({sp.x, (base + spire) * 0.5f, sp.z}, {1.3f, (spire - base) * 0.5f, 1.3f}, rgba(140, 130, 118));
        addBlock({sp.x, spire + 0.15f, sp.z}, {1.5f, 0.15f, 1.5f}, rgba(170, 160, 144));
        Vec3 face = std::fabs(fu.x) > std::fabs(fu.z) ? Vec3{fu.x > 0 ? -1.0f : 1.0f, 0, 0} : Vec3{0, 0, fu.z > 0 ? -1.0f : 1.0f};
        addLadder(Vec3{sp.x, base, sp.z} + face * 1.3f, spire + 0.3f, face);
        TreasureChest chest;
        chest.pos = {sp.x, spire + 0.3f, sp.z};
        chest.yaw = std::atan2(face.x, face.z);
        chest.contents = {{ItemId::Elixir, 1}, {ItemId::GoldSpore, 2}};
        chest.munny = 120;
        chests_.push_back(chest);
        Vec3 e1 = at(fin + fv * 2.8f), e2 = at(fin - fv * 2.8f), e3 = at(fin - fu * 2.5f);
        encounters_ = {{"pao_agulha", AABB{fin - Vec3{6, 2, 6} + Vec3{0, base, 0}, fin + Vec3{6, 6, 6} + Vec3{0, base, 0}},
                        {{{EnemyKind::Watcher, e1}, {EnemyKind::Watcher, e2}, {EnemyKind::Dazzled, e3}},
                         {{EnemyKind::Hound, e1}, {EnemyKind::Hound, e2}, {EnemyKind::Brute, e3}}},
                        {}, "", ""}};
        LevelPart part;
        part.title = "Pan de Azúcar";
        part.objective = "Sube a la Agulha";
        part.kind = PartKind::Encounter;
        part.target = "pao_agulha";
        part.marker = Vec3{sp.x, spire + 1.5f, sp.z};
        parts_ = {part};
    }
    nextLevel_ = LevelId::Rio;
}
