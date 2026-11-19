// Las Cumbres Nevadas: un valle alto cubierto de nieve entre picos, de día y nevando.
//   - El refugio: la casa de troncos del guarda (Anselmo: se descansa junto al fuego), los
//     cobertizos, la leñera, el trineo y el muñeco de nieve; el humo de la chimenea.
//   - El camino sube del sur del valle al refugio y de allí salen tres: las revueltas al
//     collado y a la cumbre (la cruz de hierro y su cápsula), el que baja al lago helado (al
//     oeste, en su hoya) y el del barranco (al este), que se cruza por una pasarela de madera
//     hasta la cabaña abandonada.
//   - Abetos nevados, rocas con nieve, faroles en los caminos, Grises salvajes y tres nodos.

#include "game/game.h"
#include "game/map_tools.h"
#include "render/debug_draw.h"

#include <algorithm>
#include <cmath>

using namespace maptools;

void Game::buildCumbres() {
    levelName_ = "Cumbres Nevadas";
    floorColor_ = rgba(236, 240, 246);
    floorMaterial_ = Material::Snow;
    night_ = 0.22f;      // de día, con el cielo cubierto
    wildSpawns_ = true;
    fogScale_ = 0.42f;
    darkScale_ = 0.5f;
    viewDistance_ = 420;
    snowfall_ = true;
    const float kSize = 440, kHalf = kSize * 0.5f;
    makeTerrain(kSize, 1.5f);
    Terrain& t = const_cast<Terrain&>(*world_.terrain);
    Rng rng{7341};

    const Vec3 refuge{10, 0, 0};
    const std::vector<Vec3> road = {{0, 0, -185}, {6, 0, -140}, {-10, 0, -95}, {2, 0, -55}, {12, 0, -22}, {10, 0, -8}};
    const std::vector<Vec3> toSummit = {{14, 0, 10},  {40, 0, 34},  {22, 0, 58},  {52, 0, 84},
                                        {34, 0, 108}, {62, 0, 132}, {70, 0, 156}, {86, 0, 176}};
    const std::vector<Vec3> toLake = {{2, 0, -9}, {-34, 0, 12}, {-66, 0, 34}, {-86, 0, 46}};
    const std::vector<Vec3> toRavine = {{22, 0, -4}, {56, 0, -18}, {86, 0, -26}, {114, 0, -26}, {140, 0, -36}};
    const std::vector<const std::vector<Vec3>*> paths = {&road, &toSummit, &toLake, &toRavine};
    auto pathDist = [&](Vec3 p) {
        float d = 1e9f;
        for (const auto* pl : paths) d = std::min(d, distToPath(p, *pl));
        return d;
    };
    const Vec3 summit = toSummit.back(), col = toSummit[6], lake{-106, 0, 56};
    const float kRavineX = 100;  // el barranco corre de norte a sur por aquí
    struct Peak {
        Vec3 c;
        float h, r;
    };
    const Peak peaks[] = {{summit + Vec3{6, 0, 10}, 92, 40}, {{-130, 0, 165}, 84, 52}, {{175, 0, 90}, 72, 46}, {{-175, 0, -55}, 62, 50},
                          {{160, 0, -150}, 58, 46},          {{-60, 0, -170}, 40, 50},  {{30, 0, 210}, 70, 44}};

    for (int iz = 0; iz < t.nz; ++iz)
        for (int ix = 0; ix < t.nx; ++ix) {
            float x = t.x0 + float(ix) * t.cell, z = t.z0 + float(iz) * t.cell;
            Vec3 p{x, 0, z};
            float h = 8.0f + 5.0f * (fbm(x / 90.0f, z / 90.0f, 41) + 1.0f) + 1.0f * fbm(x / 20.0f, z / 20.0f, 42);
            for (const Peak& k : peaks) {
                float d2 = dot(flat(p - k.c), flat(p - k.c));
                h += k.h * std::exp(-d2 / (2 * k.r * k.r));
            }
            float dp = pathDist(p);
            // Lejos del valle, crestas afiladas (más calmadas junto a los caminos).
            float wild = smoothstep01(clampf((length(flat(p - refuge)) - 80.0f) / 90.0f, 0, 1)) * (1.0f - smoothFalloff(dp, 22.0f));
            float ridge = 1.0f - std::fabs(noise(x / 34.0f, z / 34.0f, 43));
            h += wild * (24.0f * ridge * ridge + 6.0f * fbm(x / 12.0f, z / 12.0f, 44));
            float edge = std::min({x - t.x0, -t.x0 - x, z - t.z0, -t.z0 - z});
            if (edge < 50) h += 70.0f * (1 - edge / 50) * (1 - edge / 50);
            // El llano del refugio, la hoya del lago y el barranco.
            h = h + (12.0f - h) * smoothFalloff(length(flat(p - refuge)), 34.0f);
            float dl = length(flat(p - lake));
            h = h + (14.0f - h) * smoothFalloff(dl, 34.0f);
            float dr = std::fabs(x - kRavineX + 4.0f * std::sin(z / 23.0f));
            if (z > -120 && z < 60) h -= 22.0f * smoothFalloff(dr, 7.5f) * smoothstep01(clampf((60 - z) / 30, 0, 1));
            // Los caminos: el relieve se suaviza (terraza) a su paso.
            float hp = h * 0.94f + 0.5f;
            h = h + (hp - h) * smoothFalloff(dp, 5.0f + 4.0f * wild);
            if (dl < 22) h = std::min(h, 14.0f);  // el hielo, liso
            t.at(ix, iz) = h;
        }
    // La cima y el collado, llanos alrededor del final de su camino.
    for (Vec3 c : {summit, col}) {
        float top = t.heightAt(c.x, c.z);
        for (int iz = 0; iz < t.nz; ++iz)
            for (int ix = 0; ix < t.nx; ++ix) {
                float x = t.x0 + float(ix) * t.cell, z = t.z0 + float(iz) * t.cell;
                float k = smoothFalloff(length(flat(Vec3{x, 0, z} - c)), 11.0f);
                t.at(ix, iz) += (top - t.at(ix, iz)) * k;
            }
    }
    // Color: nieve casi en todo; roca en lo empinado; el hielo del lago; los caminos pisados.
    for (int iz = 0; iz < t.nz; ++iz)
        for (int ix = 0; ix < t.nx; ++ix) {
            float x = t.x0 + float(ix) * t.cell, z = t.z0 + float(iz) * t.cell;
            Vec3 p{x, 0, z};
            float slope = 1.0f - t.normalAt(x, z).y;
            float v = noise(x / 6.0f, z / 6.0f, 45) * 0.5f + 0.5f;
            uint32_t col2 = mixRgb(rgba(232, 236, 244), rgba(246, 248, 252), v);
            Material m = Material::Snow;
            float rock = clampf((slope - 0.42f) / 0.15f, 0, 1);
            if (rock > 0) col2 = mixRgb(col2, mixRgb(rgba(96, 94, 98), rgba(122, 118, 116), v), rock);
            if (rock > 0.6f) m = Material::Stone;
            if (length(flat(p - lake)) < 21.5f) col2 = mixRgb(rgba(168, 206, 226), rgba(196, 226, 240), v), m = Material::Metal;  // el hielo
            float dp = pathDist(p);
            if (dp < 2.0f) col2 = mixRgb(rgba(206, 210, 222), rgba(186, 190, 204), v), m = Material::Snow;  // nieve pisada
            paintTerrain(ix, iz, col2, m);
        }
    auto ground = [&](float x, float z) { return t.heightAt(x, z); };
    auto at = [&](Vec3 p) { return Vec3{p.x, ground(p.x, p.z), p.z}; };

    // Sitios libres de árboles: el refugio, la llegada, los nodos, las cápsulas, la cima, el lago.
    std::vector<std::pair<Vec3, float>> clearings = {{refuge, 26}, {road.front(), 18}, {summit, 12}, {col, 10}, {lake, 26},
                                                     {toRavine.back(), 10}, {{kRavineX, 0, -26}, 12}};
    auto clear = [&](Vec3 p, float pathGap) {
        if (pathDist(p) < pathGap || std::fabs(p.x) > kHalf - 30 || std::fabs(p.z) > kHalf - 30) return false;
        if (std::fabs(p.x - kRavineX + 4.0f * std::sin(p.z / 23.0f)) < 9 && p.z > -125 && p.z < 62) return false;
        for (const auto& [c, r] : clearings)
            if (length(flat(p - c)) < r) return false;
        return true;
    };

    // El refugio, de cara al camino que llega; los cobertizos, la leñera y lo demás.
    addProp("refugio", refuge + Vec3{0, 0, 8}, kPi);
    addProp("cobertizo", refuge + Vec3{-12, 0, 6}, kPi * 0.5f);
    addProp("cobertizo_cerrado", refuge + Vec3{12, 0, 9}, -kPi * 0.5f);
    addProp("lenera", refuge + Vec3{-6.5f, 0, 1.0f}, kPi);
    addProp("trineo", refuge + Vec3{-9, 0, -1.5f}, 0.5f);
    addProp("muneco_nieve", refuge + Vec3{7, 0, -6}, kPi + 0.4f);
    addProp("banca_parque", refuge + Vec3{-3, 0, -5}, kPi + 0.2f);
    for (int k = 0; k < 6; ++k) addProp("valla", refuge + Vec3{-18.0f + 3.0f * float(k), 0, 14}, 0);
    chimneys_ = {at(refuge + Vec3{0, 0, 8}) + Vec3{-3.0f, 9.5f, 2.4f}};
    // Faroles a lo largo de los caminos.
    for (const auto* pl : paths) {
        float acc = 0;
        for (size_t i = 0; i + 1 < pl->size(); ++i) {
            Vec3 a = (*pl)[i], b = (*pl)[i + 1], d = flat(b - a);
            float len = length(d);
            Vec3 side = normalize(Vec3{d.z, 0, -d.x});
            for (float s = 0; s < len; s += 2) {
                acc += 2;
                if (acc < 30) continue;
                acc = 0;
                Vec3 p = a + d * (s / len) + side * 2.6f;
                if (length(flat(p - refuge)) < 18 || std::fabs(p.x - kRavineX) < 10) continue;
                addProp("farol_camino", p, std::atan2(-side.x, -side.z));
            }
        }
        addProp("senal_camino", pl->front() + normalize(flat((*pl)[1] - pl->front())) * 6.0f + Vec3{2.8f, 0, 0}, rng.range(0, 6.28f));
    }
    // La pasarela del barranco, entre los dos bordes (a la altura del más bajo).
    {
        Vec3 c{kRavineX - 4.0f * std::sin(-26.0f / 23.0f), 0, -26};
        float bank = std::min(ground(c.x - 8, c.z), ground(c.x + 8, c.z));
        addProp("pasarela", {c.x, bank - 0.05f, c.z}, kPi * 0.5f, 1.15f, false);
    }
    // La cabaña abandonada al otro lado, con su cápsula.
    addProp("cobertizo_cerrado", toRavine.back() + Vec3{4, 0, -5}, 0.3f);
    // La cima: la cruz y un hito; el collado, con dos rocas.
    addProp("cruz_cumbre", summit + Vec3{0, 0, 3});
    addProp("roca_nevada", col + Vec3{-5, 0, 3}, 0.4f);
    addProp("roca_nevada", col + Vec3{6, 0, -2}, 2.1f, 0.8f);
    // El lago helado: rocas en la orilla y un trineo olvidado.
    for (int k = 0; k < 9; ++k) {
        float a = float(k) / 9 * 2 * kPi + 0.3f;
        addProp(k % 3 ? "roca_nevada" : "roca_nevada_grande", lake + Vec3{std::cos(a), 0, std::sin(a)} * 24.0f, a, rng.range(0.7f, 1.1f));
    }
    addProp("trineo", lake + Vec3{-6, 0, 4}, 1.9f);

    // Abetos: bosques en las laderas bajas (claros con los caminos) y sueltos más arriba.
    const char* firs[] = {"abeto_nevado_1", "abeto_nevado_2", "abeto_nevado_3"};
    int planted = 0;
    for (int tries = 0; tries < 9000 && planted < 900; ++tries) {
        Vec3 p{rng.range(-kHalf + 30, kHalf - 30), 0, rng.range(-kHalf + 30, kHalf - 30)};
        if (!clear(p, 4.0f)) continue;
        float g = ground(p.x, p.z), slope = 1.0f - t.normalAt(p.x, p.z).y;
        if (slope > 0.38f || g > 70) continue;
        float forest = noise(p.x / 45.0f, p.z / 45.0f, 46) * 0.5f + 0.5f;
        if (rng.next() > forest * forest * 1.6f * (g < 40 ? 1.0f : 0.4f)) continue;
        addProp(firs[planted % 3], p, rng.range(0, 6.28f), rng.range(0.8f, 1.25f));
        ++planted;
    }
    for (int k = 0, placed = 0; k < 600 && placed < 90; ++k) {  // rocas con nieve
        Vec3 p{rng.range(-kHalf + 30, kHalf - 30), 0, rng.range(-kHalf + 30, kHalf - 30)};
        if (!clear(p, 3.0f)) continue;
        addProp(placed % 4 ? "roca_nevada" : "roca_nevada_grande", p, rng.range(0, 6.28f), rng.range(0.6f, 1.3f));
        ++placed;
    }

    // La gente del refugio.
    addNpc("abuelo", refuge + Vec3{1.0f, 0, 2.6f}, 1.5f, "Anselmo, el guarda", NpcRole::Inn,
           {"Pasa, que el fuego no se apaga. Lo único que no se paró aquí arriba es el frío.",
            "Arriba, en la cruz, dejé una caja para quien llegue. Sube por las revueltas, no por la pala."});
    addNpc("deshollinadora", refuge + Vec3{-6.5f, 0, -1.5f}, 2.0f, "Marta, la leñadora", NpcRole::Townsfolk,
           {"La leña no se acaba porque no se gasta. Corto igual: si paro, se me hiela el hacha.",
            "Al este, el barranco. La pasarela aguanta, pero no mires abajo."});
    addNpc("nino_sombras", refuge + Vec3{8.5f, 0, -4.5f}, 3.0f, "Tito", NpcRole::Townsfolk,
           {"El muñeco lo hice antes de que se parara la hora. No se derrite. Ni crece.",
            "En el lago helado se patina. Bueno, se resbala, que es lo mismo."});

    // Cápsulas.
    auto chest = [&](Vec3 p, float yaw, std::vector<std::pair<ItemId, int>> contents, int munny) {
        TreasureChest c;
        c.pos = at(p);
        c.yaw = yaw;
        c.contents = std::move(contents);
        c.munny = munny;
        chests_.push_back(c);
    };
    chest(summit + Vec3{0, 0, -2}, 0, {{ItemId::Elixir, 1}, {ItemId::BlazeGem, 2}}, 120);
    chest(toRavine.back() + Vec3{1, 0, -1}, 0.3f, {{ItemId::HiPotion, 2}, {ItemId::ShinyShard, 3}}, 60);
    chest(lake + Vec3{0, 0, 0}, kPi, {{ItemId::Ether, 2}, {ItemId::GoldSpore, 2}}, 40);
    chest(col + Vec3{2, 0, 4}, 1.2f, {{ItemId::Potion, 3}}, 25);

    heroStart_ = at(road.front() + Vec3{0, 0, 4});
    heroStartYaw_ = 0;
    arrivals_.push_back({heroStart_, heroStartYaw_});
    const Vec3 nodeSpots[] = {refuge + Vec3{-2, 0, -12}, lake + Vec3{0, 0, -24}, col + Vec3{-3, 0, -4}};
    const uint32_t nodeColors[] = {rgba(255, 190, 60), rgba(40, 220, 255), rgba(255, 70, 190)};
    for (int k = 0; k < 3; ++k) nodes_.push_back({"nodo_cumbres_" + std::to_string(k), at(nodeSpots[k]), 34, nodeColors[k], ""});
    for (Vec3 p : {road.front() + Vec3{6, 0, 14}, toSummit[3] + Vec3{3, 0, 0}}) addPortal(at(p));
    LevelPart explore{"Cumbres Nevadas", "Enciende los tres nodos de las cumbres", PartKind::Nodes, "", 3, {}, nodeSpots[0], {}, "", ""};
    parts_ = {explore};
    nextLevel_ = LevelId::Cumbres;
}
