// La Gruta de las Peñas: zona a la que se entra por la boca de la peña, al final del camino del
// este del Campo abierto (Game::goToZone). Diez salas talladas en el relieve, cada una con su
// techo, unidas por túneles; alumbradas por cristales, antorchas y faroles (que hacen de punto
// de control al encenderlos).
//   1. La Boca: la luz de fuera aún llega; la salida al campo.
//   2. La Sala de las Raíces: raíces que bajan del techo, setas que brillan; perros de los Grises.
//   3. El Río Negro: lo cruza un puente de roca; del agua salen los cangrejos de cristal.
//   4. La Galería de Cristales: el haz de un cristal, dos espejos que girar (F) y la puerta que
//      abre el receptor.
//   5. La Sima: un foso de lado a lado que se salta por pilares; quien cae, sube por la escalera.
//   6. El Pozo: se sube por una escalera de gato a la galería de arriba.
//   7. El Santuario: el altar, Pabilo (que vende) y un farol para descansar.
//   8. El Nido: los Grises, en tres oleadas.
//   9. El Lago Subterráneo: un islote con su cápsula, a saltos por dos piedras (y si se cae al
//      agua, una escalera para salir).
//  10. El Corazón de Piedra: la Madre de Piedra; detrás, la grieta que sale de nuevo al campo.

#include "game/game.h"
#include "game/map_tools.h"
#include "render/debug_draw.h"

#include <algorithm>
#include <cmath>
#include <iterator>

using namespace maptools;

namespace {

struct Room {
    Vec3 c;
    float rx, rz, h;
};
struct Tunnel {  // tramo de túnel de `a` (a la altura ha) a `b` (hb), de medio ancho r
    Vec3 a, b;
    float ha, hb, r;
};

constexpr float kWater = -3.5f;  // el nivel del agua del río y del lago

// Las salas (índice: número de sala − 1; la 10 es la galería de arriba del Pozo).
const Room kRooms[] = {
    {{0, 0, 0}, 5.5f, 7, 0},        // 1 la Boca
    {{2, 0, 24}, 8, 7, -1},         // 2 las Raíces
    {{-18, 0, 42}, 10, 8, -2},      // 3 el Río Negro
    {{-38, 0, 62}, 9, 8, -2},       // 4 la Galería de Cristales
    {{-24, 0, 84}, 12, 8, 3},       // 5 la Sima
    {{0, 0, 98}, 6, 6, 3},          // 6 el Pozo (abajo)
    {{22, 0, 116}, 6.5f, 6, 11},    // 7 el Santuario
    {{46, 0, 98}, 11, 9, 11},       // 8 el Nido
    {{46, 0, 64}, 12, 10, 2},       // 9 el Lago
    {{22, 0, 50}, 10, 9, 2},        // 10 el Corazón
    {{0, 0, 108.5f}, 6.5f, 3, 11},  // el Pozo, la galería de arriba
};
const Tunnel kTunnels[] = {
    {{0, 0, 6}, {1, 0, 18}, 0, -1, 2.2f},          {{-3, 0, 28}, {-11, 0, 36}, -1, -2, 2.0f},
    {{-24, 0, 48}, {-33, 0, 56}, -2, -2, 2.0f},     {{-38, 0, 69}, {-37, 0, 74}, -2, 0.5f, 1.8f},
    {{-37, 0, 74}, {-33, 0, 79}, 0.5f, 3, 1.8f},    {{-13, 0, 86}, {-6, 0, 93}, 3, 3, 2.0f},
    {{5, 0, 109}, {15, 0, 114}, 11, 11, 2.0f},      {{28, 0, 113}, {37, 0, 104}, 11, 11, 2.2f},
    {{48, 0, 89}, {52, 0, 81}, 11, 7, 2.0f},        {{52, 0, 81}, {50, 0, 75}, 7, 2, 2.0f},
    {{35, 0, 61}, {31, 0, 56}, 2, 2, 2.0f},         {{14, 0, 45}, {9, 0, 38}, 2, 4, 1.5f},
};

float tunnelDist(const Tunnel& t, Vec3 p, float* h) {
    Vec3 ab = flat(t.b - t.a), ap = flat(p - t.a);
    float u = clampf(dot(ap, ab) / std::max(1e-4f, dot(ab, ab)), 0, 1);
    if (h) *h = t.ha + (t.hb - t.ha) * u;
    return length(ap - ab * u) - t.r;
}

// Lo que se pisa en cada sala, con lo que tiene recortado: el río (no se pisa, salvo el puente
// de roca), el foso de la Sima (abajo, a −3), el lago (el fondo, bajo el agua) con su islote y
// sus dos piedras.
const Vec3 kRiver{-18, 0, 42};
const float kRiverHalf = 1.7f, kBridgeHalf = 1.2f;
const Vec3 kPit = kRooms[4].c;
const float kPitHalf = 5.0f, kPitFloor = -3.0f;
const Vec3 kLake{46, 0, 61.5f}, kIsland{46, 0, 60.5f};
const Vec3 kStones[] = {{46.5f, 0, 65.6f}};
const float kLakeBed = -4.5f;

float roomDist(int i, Vec3 p) {
    const Room& r = kRooms[i];
    float wobble = 0.7f * noise(p.x / 3.0f, p.z / 3.0f, 70u + uint32_t(i));
    return sdBox(p, r.c, r.rx, r.rz, std::min(r.rx, r.rz) * 0.7f) + wobble;
}

bool inRiver(Vec3 p) { return std::fabs(p.z - kRiver.z) < kRiverHalf && std::fabs(p.x - kRiver.x) > kBridgeHalf; }
bool inPit(Vec3 p) { return std::fabs(p.x - kPit.x) < kPitHalf; }
bool inLake(Vec3 p) {
    Vec3 q = flat(p - kLake);
    return (q.x * q.x) / (8.0f * 8.0f) + (q.z * q.z) / (5.5f * 5.5f) < 1.0f;
}
bool onIslandOrStone(Vec3 p) {
    if (length(flat(p - kIsland)) < 2.2f) return true;
    for (Vec3 s : kStones)
        if (length(flat(p - s)) < 0.65f) return true;
    return false;
}

// Distancia (con signo) a lo que se pisa y, dentro, la altura del suelo.
float walkDist(Vec3 p, float* floorH) {
    float best = 1e9f, h = 0;
    for (int i = 0; i < int(std::size(kRooms)); ++i) {
        float d = roomDist(i, p);
        float fh = kRooms[i].h;
        if (i == 2 && d < 0 && inRiver(p)) d = std::max(d, kRiverHalf - std::fabs(p.z - kRiver.z));  // el agua no se pisa
        if (i == 4 && d < 0 && inPit(p)) fh = kPitFloor;
        if (i == 8 && d < 0 && inLake(p)) fh = onIslandOrStone(p) ? kRooms[8].h : kLakeBed;
        if (d < best) best = d, h = fh;
    }
    for (const Tunnel& t : kTunnels) {
        float th = 0, d = tunnelDist(t, p, &th);
        if (d < best) best = d, h = th;
    }
    if (floorH) *floorH = h;
    return best;
}

float riseWall(float d) { return d <= 0 ? 0.0f : 14.0f * smoothstep01(d / 2.0f) + 1.5f * std::max(0.0f, d - 2.0f); }

const uint32_t kRock = rgba(132, 122, 112), kRockDark = rgba(92, 86, 82), kCyan = rgba(90, 230, 255), kViolet = rgba(184, 140, 255),
               kGreen = rgba(127, 255, 160), kAmber = rgba(255, 190, 100);

}  // namespace

void Game::buildGruta() {
    levelName_ = "Gruta de las Peñas";
    night_ = 0.72f;
    darkScale_ = 0.45f;
    fogScale_ = 0.9f;
    viewDistance_ = 90;
    floorColor_ = rgba(54, 50, 60);
    floorMaterial_ = Material::Stone;
    makeTerrain(280, 0.7f);
    Terrain& t = const_cast<Terrain&>(*world_.terrain);
    t.fence.assign(t.heights.size(), 0);
    Rng rng{4417};

    // El relieve: dentro de lo que se pisa, su suelo; fuera, paredes que suben enseguida.
    for (int iz = 0; iz < t.nz; ++iz)
        for (int ix = 0; ix < t.nx; ++ix) {
            float x = t.x0 + float(ix) * t.cell, z = t.z0 + float(iz) * t.cell;
            Vec3 p{x, 0, z};
            float fh = 0, d = walkDist(p, &fh);
            size_t idx = size_t(iz) * size_t(t.nx) + size_t(ix);
            t.fence[idx] = d;
            float h;
            if (d <= 0) {
                h = fh + 0.15f * noise(x * 0.8f, z * 0.8f, 5);
            } else {
                h = 40;
                for (int i = 0; i < int(std::size(kRooms)); ++i) h = std::min(h, kRooms[i].h + riseWall(roomDist(i, p)));
                for (const Tunnel& tn : kTunnels) {
                    float th = 0, td = tunnelDist(tn, p, &th);
                    h = std::min(h, th + riseWall(td));
                }
                if (inRiver(p) && roomDist(2, p) < 0) h = -4.4f;  // el cauce
                h += 0.8f * fbm(x / 2.5f, z / 2.5f, 9) * smoothstep01(d / 1.5f);
            }
            t.at(ix, iz) = h;
            float n = noise(x / 2.0f, z / 2.0f, 13) * 0.5f + 0.5f;
            uint32_t col = d <= 0 ? mixRgb(rgba(118, 108, 98), rgba(140, 128, 114), n) : mixRgb(kRockDark, kRock, n);
            if (d <= 0 && fh < kWater) col = rgba(60, 64, 70);          // el fondo, bajo el agua
            if (d <= 0 && roomDist(1, p) < 0) col = mixRgb(col, rgba(70, 92, 60), 0.4f);  // musgo en las Raíces
            paintTerrain(ix, iz, col, Material::Stone);
        }
    waterLevel_ = kWater;
    auto ground = [&](float x, float z) { return t.heightAt(x, z); };
    auto at = [&](Vec3 p) { return Vec3{p.x, ground(p.x, p.z), p.z}; };
    const Mat4 id = Mat4::identity();

    // Los techos: sobre cada sala y a lo largo de los túneles (las paredes suben por encima).
    for (int i = 0; i < int(std::size(kRooms)); ++i) {
        const Room& r = kRooms[i];
        float top = (i == 5 ? 11.0f : r.h) + (i == 4 || i == 8 ? 9.5f : 8.0f);
        addBlock({r.c.x, top + 0.6f, r.c.z}, {r.rx + 4, 0.6f, r.rz + 4}, kRockDark);
    }
    for (const Tunnel& tn : kTunnels) {
        float len = length(flat(tn.b - tn.a));
        for (float s = 0; s <= len; s += 2.0f) {
            float u = s / std::max(len, 1e-3f);
            Vec3 c = tn.a + (tn.b - tn.a) * u;
            float h = tn.ha + (tn.hb - tn.ha) * u;
            addBlock({c.x, h + 5.3f, c.z}, {tn.r + 1.6f, 0.5f, tn.r + 1.6f}, kRockDark);
        }
    }
    // Estalactitas del techo, estalagmitas y piedras por el suelo, en todas las salas.
    const char* hang[] = {"estalactita_1", "estalactita_2", "estalactita_3"};
    const char* stand[] = {"estalagmita_1", "estalagmita_2", "estalagmita_3"};
    for (int i = 0; i < int(std::size(kRooms)); ++i) {
        const Room& r = kRooms[i];
        float top = (i == 5 ? 11.0f : r.h) + (i == 4 || i == 8 ? 9.5f : 8.0f);
        int n = int(r.rx * r.rz / 9);
        for (int k = 0; k < n; ++k) {
            Vec3 p = r.c + Vec3{rng.range(-r.rx, r.rx), 0, rng.range(-r.rz, r.rz)};
            addProp(hang[k % 3], {p.x, top, p.z}, rng.range(0, 6.28f), rng.range(0.8f, 1.3f), false);
        }
        for (int k = 0; k < n / 2; ++k) {  // al pie de las paredes, donde no estorban
            float a = rng.range(0, 6.28f);
            Vec3 p = r.c + Vec3{std::cos(a) * r.rx * 0.95f, 0, std::sin(a) * r.rz * 0.95f};
            float fh = 0;
            float d = walkDist(p, &fh);
            if (d > -0.3f || d < -2.5f) continue;
            addProp(k % 2 ? stand[k % 3] : (k % 3 ? "piedra" : "roca"), at(p), rng.range(0, 6.28f), rng.range(0.7f, 1.1f));
        }
    }
    // Antorchas a lo largo de los túneles.
    for (const Tunnel& tn : kTunnels) {
        Vec3 d = normalize(flat(tn.b - tn.a)), side{-d.z, 0, d.x};
        Vec3 mid = (tn.a + tn.b) * 0.5f + side * (tn.r - 0.35f);
        addTorch(at(mid));
    }
    auto crystal = [&](Vec3 p, uint32_t color, const char* prop) {
        addProp(prop, at(p), rng.range(0, 6.28f));
        Lamp l{at(p) + Vec3{0, 0.9f, 0}, color};
        l.style = LampStyle::Crystal;
        l.bulb = false;
        l.reach = 0.9f;
        lamps_.push_back(l);
    };
    auto lantern = [&](Vec3 p) {
        Lantern l;
        l.pos = at(p);
        lanterns_.push_back(l);
    };
    auto chest = [&](Vec3 p, float yaw, std::vector<std::pair<ItemId, int>> contents, int munny) {
        TreasureChest c;
        c.pos = at(p);
        c.yaw = yaw;
        c.contents = std::move(contents);
        c.munny = munny;
        chests_.push_back(c);
    };
    auto enemyAt = [&](EnemyKind k, Vec3 p) { return Spawn{k, at(p) + Vec3{0, 0.2f, 0}}; };
    auto zoneBox = [&](int room, float up = 6) {
        const Room& r = kRooms[room];
        return AABB{{r.c.x - r.rx, r.h - 6, r.c.z - r.rz}, {r.c.x + r.rx, r.h + up, r.c.z + r.rz}};
    };

    // 1. La Boca: la claridad de fuera, la puerta al campo, un farol y las primeras raíces.
    addDecor(id, {0, 2.0f, -7.8f}, {2.0f, 2.0f, 0.1f}, rgba(150, 126, 96), Material::Glow, false);
    doors_.push_back({at({0, 0, -5.8f}), LevelId::Campo, arrival::kCampoGruta, "Salir · al campo"});
    lantern({-3.5f, 0, 2.5f});
    addProp("raices_colgantes", {2.5f, 8.0f, 3.0f}, 0.4f, 1.0f, false);

    // 2. Las Raíces: raíces y setas; perros de los Grises.
    for (Vec3 p : {Vec3{-2, 0, 22}, Vec3{5, 0, 26}, Vec3{0, 0, 29}, Vec3{7, 0, 20}, Vec3{-5, 0, 26}})
        addProp("raices_colgantes", {p.x, kRooms[1].h + 8.0f, p.z}, rng.range(0, 6.28f), rng.range(0.9f, 1.3f), false);
    for (Vec3 p : {Vec3{-4, 0, 20}, Vec3{7, 0, 27}, Vec3{3, 0, 29.5f}, Vec3{-6, 0, 24}}) addProp("setas_cueva", at(p), rng.range(0, 6.28f));
    crystal({6, 0, 21}, kGreen, "cristales_verde");
    crystal({-5, 0, 28}, kGreen, "cristales_verde");

    // 3. El Río Negro: el agua, el puente de roca (con pretiles de piedra) y los cangrejos.
    for (float s : {-1.0f, 1.0f})
        addBlock({kRiver.x + s * (kBridgeHalf + 0.15f), -1.65f, kRiver.z}, {0.2f, 0.35f, kRiverHalf + 0.3f}, kRock);
    crystal({-25, 0, 37}, kCyan, "cristales_cian");
    crystal({-10, 0, 47}, kCyan, "cristales_cian");
    addProp("columna_cueva", at({-26, 0, 47}), 0.3f);

    // 4. La Galería de Cristales: el haz sale de un cristal en la pared del oeste; dos espejos
    //    (mal girados) pueden llevarlo al receptor, que abre la puerta del túnel del norte.
    {
        const float y = kRooms[3].h;
        emitters_ = {{{-45.4f, y + 1.4f, 58}, {1, 0, 0}, rgba(140, 230, 255)}};
        mirrors_ = {{{-41, y, 58}, 0, 0}, {{-41, y, 66}, 0, 0}};
        receptors_ = {{"receptor_galeria", {-31.5f, y + 1.4f, 66}, 0, "gruta_galeria", ""}};
        addGate("gruta_galeria", {-38, y + 1.6f, 70.2f}, {2.2f, 1.6f, 0.25f}, kCyan, true);
        addDecor(id, {-46.0f, y + 1.4f, 58}, {0.4f, 0.6f, 0.4f}, kCyan, Material::Glow, true);
        for (Vec3 p : {Vec3{-45, 0, 55.5f}, Vec3{-31, 0, 56}, Vec3{-45, 0, 68}, Vec3{-34, 0, 60.5f}})
            crystal(p, p.x < -40 ? kViolet : kCyan, p.x < -40 ? "cristales_violeta" : "cristales_cian");
    }

    // 5. La Sima: el foso de lado a lado; pilares para saltarlo y, abajo, la escalera de vuelta.
    {
        const float tops[] = {3.0f, 3.5f, 4.0f, 3.4f};
        for (int k = 0; k < 4; ++k) {
            float x = kPit.x - 3.9f + float(k) * 2.6f, z = kPit.z + (k % 2 ? 1.0f : -1.0f);
            float hgt = tops[k] - kPitFloor;
            addBlock({x, kPitFloor + hgt * 0.5f, z}, {0.6f, hgt * 0.5f, 0.6f}, k % 2 ? kRock : kRockDark);
        }
        addBlock({kPit.x - kPitHalf - 0.35f, 0, kPit.z + 4}, {0.35f, 3.0f, 1.2f}, kRock);  // la pared de la escalera
        addLadder({kPit.x - kPitHalf, kPitFloor, kPit.z + 4}, kRooms[4].h, {1, 0, 0});
        lantern({kPit.x - 9, 0, kPit.z - 3});
        crystal({kPit.x - 10, 0, kPit.z + 5}, kViolet, "cristales_violeta");
        crystal({kPit.x + 9.5f, 0, kPit.z - 4}, kViolet, "cristales_violeta");
        crystal({kPit.x, 0, kPit.z + 6}, kCyan, "cristales_cian");  // abajo, en el foso
    }

    // 6. El Pozo: de la sala de abajo a la galería de arriba, por la escalera de gato.
    addBlock({0, 7.0f, 104.9f}, {1.6f, 4.0f, 0.4f}, kRock);
    addLadder({0, kRooms[5].h, 104.5f}, kRooms[10].h, {0, 0, -1});
    crystal({-4, 0, 95}, kGreen, "cristales_verde");
    crystal({4, 0, 109}, kGreen, "cristales_verde");

    // 7. El Santuario: el altar, Pabilo y el farol.
    addProp("altar_gruta", at({22, 0, 120.5f}), kPi);
    lantern({18, 0, 114});
    addNpc("pabilo", {25, 0, 113.5f}, 0.6f, "Pabilo", NpcRole::Shop,
           {"Bajé a por cera de las piedras y me quedé. Aquí abajo la hora no se nota.",
            "Lo que tengo lo cambio por lúmenes. Los Grises del Nido no aceptan nada."});
    npcs_.back().stock = {{ItemId::Potion, 28}, {ItemId::Ether, 45}, {ItemId::HiPotion, 75}, {ItemId::Elixir, 240}};
    crystal({17, 0, 119}, kAmber, "cristales_verde");

    // 8. El Nido: tres oleadas de Grises.
    for (Vec3 p : {Vec3{38, 0, 92}, Vec3{54, 0, 104}, Vec3{55, 0, 92}}) crystal(p, kViolet, "cristales_violeta");
    addProp("columna_cueva", at({42, 0, 100}), 0.8f);
    addProp("columna_cueva", at({51, 0, 95}), 2.1f);

    // 9. El Lago: el islote con su cápsula y dos saltos; la escalera para salir del agua.
    chest(kIsland, 0, {{ItemId::Elixir, 1}, {ItemId::BlazeGem, 2}}, 90);
    for (Vec3 s : kStones) addProp("estalagmita_3", at(s + Vec3{0.3f, 0, 0.3f}), 0.4f, 0.6f);
    {  // en la orilla del norte (donde acaba el lago a x = 49), de cara al agua
        float zEdge = kLake.z + 5.5f * std::sqrt(1.0f - (3.0f / 8.0f) * (3.0f / 8.0f));
        addBlock({49, (kLakeBed + kRooms[8].h) * 0.5f, zEdge + 0.35f}, {1.0f, (kRooms[8].h - kLakeBed) * 0.5f, 0.35f}, kRock);
        addLadder({49, kLakeBed, zEdge}, kRooms[8].h, {0, 0, -1});
    }
    for (Vec3 p : {Vec3{36, 0, 58}, Vec3{56, 0, 70}, Vec3{56, 0, 57}}) crystal(p, kCyan, "cristales_cian");

    // 10. El Corazón: la Madre de Piedra; la puerta se cierra durante el combate. La grieta.
    addGate("gruta_corazon", {31.5f, 3.6f, 57.5f}, {0.3f, 1.6f, 2.4f}, kAmber, false);
    for (Vec3 p : {Vec3{14, 0, 55}, Vec3{30, 0, 44}, Vec3{22, 0, 58}}) crystal(p, kAmber, "cristales_violeta");
    addProp("columna_cueva", at({17, 0, 50}), 0.2f);
    addProp("columna_cueva", at({27, 0, 52}), 1.4f);
    addProp("grieta_gruta", at({8.6f, 0, 37.0f}), std::atan2(5.0f, 7.0f));
    doors_.push_back({at({9.6f, 0, 38.6f}), LevelId::Campo, arrival::kCampoGrieta, "Salir · por la grieta"});

    encounters_ = {
        {"gruta_raices", zoneBox(1),
         {{enemyAt(EnemyKind::Hound, {-3, 0, 27}), enemyAt(EnemyKind::Hound, {6, 0, 27})},
          {enemyAt(EnemyKind::Dazzled, {2, 0, 29}), enemyAt(EnemyKind::Hound, {-5, 0, 21})}},
         {}, "", ""},
        {"gruta_rio", zoneBox(2),
         {{enemyAt(EnemyKind::Crab, {-24, 0, 45}), enemyAt(EnemyKind::Crab, {-12, 0, 39}), enemyAt(EnemyKind::Dazzled, {-18, 0, 47})},
          {enemyAt(EnemyKind::Brute, {-18, 0, 37}), enemyAt(EnemyKind::Crab, {-25, 0, 39})}},
         {}, "", ""},
        {"gruta_nido", zoneBox(7),
         {{enemyAt(EnemyKind::Hound, {40, 0, 96}), enemyAt(EnemyKind::Hound, {52, 0, 100}), enemyAt(EnemyKind::Hound, {46, 0, 104})},
          {enemyAt(EnemyKind::Dazzled, {40, 0, 102}), enemyAt(EnemyKind::Dazzled, {52, 0, 94}), enemyAt(EnemyKind::Watcher, {46, 0, 92}),
           enemyAt(EnemyKind::Watcher, {50, 0, 104})},
          {enemyAt(EnemyKind::Brute, {44, 0, 96}), enemyAt(EnemyKind::Brute, {50, 0, 100}), enemyAt(EnemyKind::Hound, {46, 0, 104})}},
         {}, "", ""},
        {"gruta_corazon", zoneBox(9),
         {{enemyAt(EnemyKind::Hound, {16, 0, 46}), enemyAt(EnemyKind::Hound, {28, 0, 54})},
          {Spawn{EnemyKind::Brute, at({22, 0, 50}) + Vec3{0, 0.2f, 0}, true, "La Madre de Piedra"},
           enemyAt(EnemyKind::Watcher, {16, 0, 54}), enemyAt(EnemyKind::Watcher, {28, 0, 46})}},
         {"gruta_corazon"}, "", ""},
    };
    LevelPart part;
    part.title = "Gruta de las Peñas";
    part.objective = "Llega al Corazón de Piedra";
    part.kind = PartKind::Encounter;
    part.target = "gruta_corazon";
    part.marker = at(kRooms[9].c) + Vec3{0, 1.5f, 0};
    parts_ = {part};

    arrivals_.push_back({at({0, 0, -3.8f}), 0});                       // por la boca, hacia dentro
    arrivals_.push_back({at({10.6f, 0, 40.0f}), std::atan2(11.4f, 10.0f)});  // por la grieta, hacia el Corazón
    heroStart_ = arrivals_[0].pos;
    heroStartYaw_ = 0;
    nextLevel_ = LevelId::Campo;
}
