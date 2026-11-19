// Mapas grandes: terreno con relieve (mapa de alturas) y escenario hecho con piezas .glb
// (assets/props, generadas por tools/props o rehechas a mano). Por ahora solo construcción:
// sin habitantes ni combates, para recorrerlos.
//   - Campo: valle abierto rodeado de montes (un pico con mirador, una sierra, peñas que corta
//     el río), seis caminos que acaban en algo, bosques grandes (pinar, robledales), una granja,
//     molinos en la loma, y cápsulas y objetos repartidos para quien explore.
//   - Ciudad Neón (de noche): manzanas de torres con ventanas encendidas, tiendas, letreros
//     verticales, vía elevada y un santuario con torii y cerezos en el centro.
//   - Pueblo cafetero (de día): plaza con iglesia y kiosco, calles de casas de bahareque de
//     colores en la loma y cafetales, plataneras, guaduales y palmas de cera alrededor.

#include "game/game.h"
#include "game/map_tools.h"
#include "game/props.h"

#include <algorithm>
#include <cmath>
#include <tuple>

using namespace maptools;

namespace {

const Vec3 kSun = normalize(Vec3{0.45f, 0.8f, 0.35f});  // la misma que el render

}  // namespace

// --- Herramientas ------------------------------------------------------------------------

void Game::makeTerrain(float size, float cell) {
    auto t = std::make_shared<Terrain>();
    t->cell = cell;
    t->nx = t->nz = int(size / cell) + 1;
    t->x0 = t->z0 = -size * 0.5f;
    t->heights.assign(size_t(t->nx) * size_t(t->nz), 0.0f);
    world_.terrain = t;
    terrainColor_.assign(t->heights.size(), rgba(80, 130, 70));
    terrainMaterial_.assign(t->heights.size(), uint8_t(Material::Grass));
    ++terrainVersion_;
}

void Game::paintTerrain(int ix, int iz, uint32_t color, Material m) {
    const Terrain& t = *world_.terrain;
    if (ix < 0 || iz < 0 || ix >= t.nx || iz >= t.nz) return;
    size_t k = size_t(iz) * size_t(t.nx) + size_t(ix);
    terrainColor_[k] = color;
    terrainMaterial_[k] = uint8_t(m);
}

bool Game::addProp(const std::string& name, Vec3 pos, float yaw, float scl, bool onGround) {
    PropLibrary& lib = propLibrary();
    int i = lib.acquire(name);  // se lee si la zona es la primera que la usa
    if (i < 0) return false;
    const PropDef& d = lib[size_t(i)];
    Mat4 local = rotateY(yaw) * scale({scl, scl, scl});
    if (onGround) {  // en una cuesta se apoya en lo más bajo (el resto queda enterrado, como un cimiento)
        float lowest = world_.groundAt(pos.x, pos.z);
        for (float sx : {0.0f, 1.0f})
            for (float sz : {0.0f, 1.0f}) {
                Vec3 c = transformPoint(local, {sx ? d.bounds.max.x : d.bounds.min.x, 0, sz ? d.bounds.max.z : d.bounds.min.z});
                lowest = std::min(lowest, world_.groundAt(pos.x + c.x * 0.8f, pos.z + c.z * 0.8f));
            }
        pos.y = lowest;
    }
    Mat4 xf = translate(pos) * local;
    auto worldBox = [&](const AABB& b) {
        AABB out{{1e9f, 1e9f, 1e9f}, {-1e9f, -1e9f, -1e9f}};
        for (int c = 0; c < 8; ++c) {
            Vec3 p = transformPoint(xf, {c & 1 ? b.max.x : b.min.x, c & 2 ? b.max.y : b.min.y, c & 4 ? b.max.z : b.min.z});
            out.min = {std::min(out.min.x, p.x), std::min(out.min.y, p.y), std::min(out.min.z, p.z)};
            out.max = {std::max(out.max.x, p.x), std::max(out.max.y, p.y), std::max(out.max.z, p.z)};
        }
        return out;
    };
    AABB wb = worldBox(d.bounds);
    props_.push_back({i, xf, wb.center(), length(wb.halfExtents())});
    for (const AABB& c : d.colliders) propSolids_.push_back(worldBox(c));
    for (const PropLight& pl : d.lights) {
        Lamp l;
        l.pos = transformPoint(xf, pl.pos);
        l.color = pl.color;
        l.style = LampStyle::Prop;
        lamps_.push_back(l);
    }
    return true;
}

// --- Campo -------------------------------------------------------------------------------

void Game::buildCampo() {
    levelName_ = "Campo abierto";
    floorColor_ = rgba(80, 130, 70);
    floorMaterial_ = Material::Grass;
    night_ = 0.42f;  // el atardecer detenido: luz baja, solo alumbran los faroles
    wildSpawns_ = true;  // Grises salvajes por el valle
    fogScale_ = 0.3f;
    darkScale_ = 0.6f;
    viewDistance_ = 480;
    const float kSize = 520;
    const float kHalf = kSize * 0.5f;
    makeTerrain(kSize, 2.5f);
    Terrain& t = const_cast<Terrain&>(*world_.terrain);

    // Caminos: el principal (de la llegada al pueblo y a la loma de los molinos) y cinco que salen
    // de él: al pinar del norte, la senda en zigzag al mirador, a las peñas del este por la
    // garganta del río, al robledal del suroeste y al bosque del oeste. Cada uno acaba en algo.
    const std::vector<Vec3> road = {{0, 0, -205}, {5, 0, -160}, {10, 0, -100}, {25, 0, -55}, {30, 0, -20}, {45, 0, 20},
                                    {52, 0, 45},  {20, 0, 75},  {-30, 0, 95},  {-80, 0, 108}, {-100, 0, 112}};
    const std::vector<Vec3> toPines = {{52, 0, 45}, {95, 0, 70}, {120, 0, 110}, {132, 0, 150}, {150, 0, 190}};
    const std::vector<Vec3> toSummit = {{-100, 0, 112}, {-126, 0, 122}, {-112, 0, 140}, {-142, 0, 150}, {-126, 0, 166},
                                        {-156, 0, 174}, {-142, 0, 192}, {-168, 0, 196}};
    const std::vector<Vec3> toCrags = {{45, 0, 20}, {90, 0, 4}, {128, 0, -14}, {162, 0, -40}, {186, 0, -72}, {200, 0, -105}};
    const std::vector<Vec3> toOaks = {{10, 0, -100}, {-38, 0, -112}, {-86, 0, -118}, {-124, 0, -140}, {-160, 0, -165}};
    const std::vector<Vec3> toWest = {{-30, 0, 95}, {-62, 0, 62}, {-100, 0, 34}, {-148, 0, 14}, {-192, 0, -6}};
    const std::vector<const std::vector<Vec3>*> paths = {&road, &toPines, &toSummit, &toCrags, &toOaks, &toWest};
    auto pathDist = [&](Vec3 p) {
        float d = 1e9f;
        for (const auto* pl : paths) d = std::min(d, distToPath(p, *pl));
        return d;
    };
    auto riverZ = [](float x) { return -30.0f + 22.0f * std::sin(x / 45.0f) + 8.0f * std::sin(x / 17.0f); };
    const Vec3 village{55, 0, 45}, hill{-110, 0, 120};
    // Montes: el pico del mirador (noroeste), la sierra del norte, las peñas del este (las corta
    // el río) y las lomas del suroeste. Fuera del valle, crestas con ruido «afilado».
    struct Peak {
        Vec3 c;
        float h, r;
    };
    const Peak peaks[] = {{{-172, 0, 196}, 62, 48}, {{-60, 0, 230}, 34, 60}, {{60, 0, 238}, 30, 55},
                          {{205, 0, -90}, 44, 42},  {{215, 0, 10}, 30, 45},  {{-175, 0, -175}, 32, 48},
                          {{150, 0, -195}, 26, 50}, {{-215, 0, 70}, 28, 45}};
    const Vec3 summit = toSummit.back();

    for (int iz = 0; iz < t.nz; ++iz)
        for (int ix = 0; ix < t.nx; ++ix) {
            float x = t.x0 + float(ix) * t.cell, z = t.z0 + float(iz) * t.cell;
            Vec3 p{x, 0, z};
            float h = 4.0f * (fbm(x / 95.0f, z / 95.0f, 11) + 1.0f) + 1.2f * fbm(x / 23.0f, z / 23.0f, 12);
            h += 16.0f * std::exp(-length(flat(p - hill)) * length(flat(p - hill)) / (2 * 38.0f * 38.0f));  // la loma del molino
            h += 9.0f * std::exp(-((x - 120) * (x - 120) + (z - 140) * (z - 140)) / (2 * 50.0f * 50.0f));    // el monte del pinar
            for (const Peak& k : peaks) {
                float d2 = dot(flat(p - k.c), flat(p - k.c));
                h += k.h * std::exp(-d2 / (2 * k.r * k.r));
            }
            // Lejos del centro del valle, el terreno se encrespa: crestas y barrancos.
            // Los caminos van por vaguadas y collados: cerca de ellos, las crestas se calman.
            float dp = pathDist(p);
            float wild = smoothstep01(clampf((length(flat(p)) - 110.0f) / 90.0f, 0, 1)) * (1.0f - smoothFalloff(dp, 26.0f));
            float ridge = 1.0f - std::fabs(noise(x / 38.0f, z / 38.0f, 21));
            h += wild * (18.0f * ridge * ridge + 6.0f * fbm(x / 14.0f, z / 14.0f, 22));
            // Bordes: montañas altas que cierran el mapa.
            float edge = std::min({x - t.x0, -t.x0 - x, z - t.z0, -t.z0 - z});
            if (edge < 50) h += 55.0f * (1 - edge / 50) * (1 - edge / 50);
            // El pueblo, en un llano; la cima del mirador, una explanada.
            float dv = length(flat(p - village));
            h = h + (4.2f - h) * smoothFalloff(dv, 42.0f);
            // Caminos: se suaviza el relieve (no se hunde); en los montes, más ancho (terraza).
            float hp = h * 0.92f + 0.3f;
            h = h + (hp - h) * smoothFalloff(dp, 6.0f + 4.0f * wild);
            // Río: cauce con orillas suaves (entre las peñas, una garganta).
            float dr = std::fabs(z - riverZ(x));
            h = h + (-1.3f - h) * smoothFalloff(dr, 9.0f) * smoothFalloff(dr, 9.0f);
            t.at(ix, iz) = std::max(h, -1.3f);
        }
    // La cima del mirador, llana: se aplana alrededor de la altura del final de la senda.
    {
        float top = t.heightAt(summit.x, summit.z);
        for (int iz = 0; iz < t.nz; ++iz)
            for (int ix = 0; ix < t.nx; ++ix) {
                float x = t.x0 + float(ix) * t.cell, z = t.z0 + float(iz) * t.cell;
                float k = smoothFalloff(length(flat(Vec3{x, 0, z} - summit)), 14.0f);
                t.at(ix, iz) += (top - t.at(ix, iz)) * k;
            }
    }
    // Color: hierba con variación, tierra en los caminos, arena en las orillas, roca en las
    // laderas empinadas y en las cumbres (con nieve sucia en lo más alto).
    for (int iz = 0; iz < t.nz; ++iz)
        for (int ix = 0; ix < t.nx; ++ix) {
            float x = t.x0 + float(ix) * t.cell, z = t.z0 + float(iz) * t.cell;
            Vec3 p{x, 0, z};
            float h = t.at(ix, iz);
            float slope = 1.0f - t.normalAt(x, z).y;
            float v = noise(x / 7.0f, z / 7.0f, 5) * 0.5f + 0.5f;
            uint32_t col = mixRgb(rgba(78, 128, 60), rgba(112, 156, 70), v);
            Material m = Material::Grass;
            float rock = std::max(clampf((h - 20) / 12, 0, 1), clampf((slope - 0.18f) / 0.15f, 0, 1));
            if (rock > 0) col = mixRgb(col, mixRgb(rgba(120, 118, 108), rgba(98, 94, 88), v), rock);
            if (rock > 0.6f) m = Material::Stone;
            if (h > 48) col = mixRgb(col, rgba(214, 212, 206), clampf((h - 48) / 10, 0, 0.85f));
            float dr = std::fabs(z - riverZ(x));
            if (dr < 7.5f) col = mixRgb(rgba(176, 160, 120), col, clampf((dr - 5.0f) / 2.5f, 0, 1)), m = Material::Cloth;
            float dp = pathDist(p);
            if (dp < 2.2f) col = mixRgb(rgba(150, 120, 82), rgba(132, 104, 70), v), m = Material::Cloth;
            paintTerrain(ix, iz, col, m);
        }
    waterLevel_ = -0.45f;

    auto ground = [&](float x, float z) { return t.heightAt(x, z); };
    // Sitios que se dejan libres de árboles y rocas: el pueblo, la llegada, los nodos, los
    // portales, las cápsulas y la cima.
    std::vector<std::pair<Vec3, float>> clearings = {{village, 40}, {road.front(), 22}, {summit, 12},
                                                     {toCrags.back() + Vec3{-2, 0, 6}, 12}};  // la boca y la grieta de la gruta
    auto clearOf = [&](Vec3 p, float pathGap, float riverGap) {
        if (pathDist(p) < pathGap || std::fabs(p.z - riverZ(p.x)) < riverGap) return false;
        if (std::fabs(p.x) > kHalf - 30 || std::fabs(p.z) > kHalf - 30) return false;
        for (const auto& [c, r] : clearings)
            if (length(flat(p - c)) < r) return false;
        return true;
    };
    Rng rng{20251116};

    // Puentes donde los caminos cruzan el río.
    for (const auto* pl : paths)
        for (size_t i = 0; i + 1 < pl->size(); ++i) {
            Vec3 a = (*pl)[i], b = (*pl)[i + 1];
            float fa = a.z - riverZ(a.x), fb = b.z - riverZ(b.x);
            if (fa * fb > 0) continue;
            Vec3 c = a + (b - a) * (fa / (fa - fb));
            float yaw = std::atan2(b.x - a.x, b.z - a.z);
            float bank = std::max(ground(c.x + std::sin(yaw) * 6, c.z + std::cos(yaw) * 6), ground(c.x - std::sin(yaw) * 6, c.z - std::cos(yaw) * 6));
            addProp("puente_madera", {c.x, bank - 0.55f, c.z}, yaw, 1.0f, false);
            clearings.push_back({c, 8});
        }
    // Faroles a lo largo de los caminos (en las sendas de monte, más espaciados).
    for (const auto* pl : paths) {
        float acc = 0, gap = pl == &road || pl == &toPines ? 28.0f : 40.0f;
        for (size_t i = 0; i + 1 < pl->size(); ++i) {
            Vec3 a = (*pl)[i], b = (*pl)[i + 1], d = flat(b - a);
            float len = length(d);
            Vec3 side = normalize(Vec3{d.z, 0, -d.x});
            for (float s = 0; s < len; s += 2) {
                acc += 2;
                if (acc < gap) continue;
                acc = 0;
                Vec3 p = a + d * (s / len) + side * 2.6f;
                if (std::fabs(p.z - riverZ(p.x)) < 9 || length(flat(p - village)) < 30) continue;
                addProp("farol_camino", p, std::atan2(-side.x, -side.z));
            }
        }
        addProp("senal_camino", pl->front() + normalize(flat((*pl)[1] - pl->front())) * 6.0f + Vec3{2.8f, 0, 0}, rng.range(0, 6.28f));
    }
    // El pueblo: tres casas, el granero, el pozo, un huerto vallado con fardos.
    addProp("casa_campo", village + Vec3{-14, 0, 12}, 3.3f);
    addProp("casa_campo_verde", village + Vec3{8, 0, 18}, 3.0f);
    addProp("casa_campo", village + Vec3{18, 0, -4}, -1.6f);
    addProp("granero", village + Vec3{-18, 0, -14}, 0.4f);
    addProp("pozo", village + Vec3{0, 0, 2});
    addProp("arbusto_flores", village + Vec3{-9, 0, 8});
    addProp("arbusto_flores", village + Vec3{12, 0, 12});
    addProp("roble_grande", village + Vec3{-4, 0, 22});
    for (int k = 0; k < 8; ++k) {  // huerto vallado
        Vec3 c = village + Vec3{22, 0, 18};
        addProp("valla", c + Vec3{-6 + 3.0f * float(k % 4), 0, k < 4 ? -6.0f : 6.0f}, 0);
        addProp("valla", c + Vec3{k < 4 ? -7.5f : 7.5f, 0, -4.5f + 3.0f * float(k % 4)}, kPi * 0.5f);
    }
    for (int k = 0; k < 6; ++k) addProp("fardo_heno", village + Vec3{18.0f + float(k % 3) * 3.5f, 0, 14.0f + float(k / 3) * 4.5f}, 0.3f * float(k));
    // La loma de los molinos.
    addProp("molino", hill + Vec3{0, 0, 0}, 2.6f);
    addProp("molino", hill + Vec3{-26, 0, 24}, 2.2f);
    addProp("molino", hill + Vec3{22, 0, 30}, 2.9f);
    clearings.push_back({hill, 14});
    clearings.push_back({hill + Vec3{-26, 0, 24}, 10});
    clearings.push_back({hill + Vec3{22, 0, 30}, 10});
    // Nodos de color (el pueblo, la loma, el puente y el pinar) y portales: con su claro.
    const Vec3 nodeSpots[] = {village + Vec3{-4, 0, -6}, hill + Vec3{0, 0, -16}, {28, 0, -14}, {104, 0, 92}};
    const Vec3 portalSpots[] = {{12, 0, -178}, {46, 0, 30}};
    for (Vec3 p : nodeSpots) clearings.push_back({p, 12});
    for (Vec3 p : portalSpots) clearings.push_back({p, 5});

    // Objetos por el mapa: cápsulas al final de cada camino y en algún rincón escondido, y
    // botín suelto (pociones, materiales, lúmenes) lejos de los caminos, para quien explore.
    auto chest = [&](Vec3 p, float yaw, std::vector<std::pair<ItemId, int>> contents, int munny) {
        TreasureChest c;
        c.pos = {p.x, ground(p.x, p.z), p.z};
        c.yaw = yaw;
        c.contents = std::move(contents);
        c.munny = munny;
        chests_.push_back(c);
        clearings.push_back({p, 6});
    };
    chest(summit + Vec3{0, 0, 4}, kPi, {{ItemId::LightLance, 1}, {ItemId::Elixir, 1}}, 80);           // la cima del mirador
    chest(toPines.back() + Vec3{3, 0, 4}, kPi, {{ItemId::ShadowClaws, 1}, {ItemId::HiPotion, 1}}, 40);  // el fondo del pinar
    chest(toCrags.back() + Vec3{4, 0, -3}, 0.5f, {{ItemId::BlazeGem, 2}, {ItemId::Ether, 1}}, 50);      // las peñas del este
    chest(toOaks.back() + Vec3{-3, 0, -4}, 0.8f, {{ItemId::HiPotion, 2}, {ItemId::GoldSpore, 2}}, 30);  // el robledal
    chest(toWest.back() + Vec3{-4, 0, 2}, -kPi * 0.5f, {{ItemId::Ether, 2}, {ItemId::WingDust, 2}}, 30); // el bosque del oeste
    chest({-150, 0, 70}, 1.2f, {{ItemId::ShinyShard, 3}, {ItemId::Potion, 2}}, 40);                    // claro escondido en el bosque
    chest({170, 0, 130}, -0.6f, {{ItemId::Elixir, 1}, {ItemId::GoldSpore, 1}}, 60);                     // en lo hondo del pinar
    chest(hill + Vec3{-16, 0, -14}, 0.3f, {{ItemId::Potion, 3}}, 20);                                  // al pie de los molinos
    {
        const ItemId loose[] = {ItemId::Potion, ItemId::Potion, ItemId::Potion, ItemId::Ether, ItemId::Ether, ItemId::HiPotion,
                                ItemId::ShinyShard, ItemId::GoldSpore, ItemId::WingDust, ItemId::BlazeGem};
        int placed = 0;
        for (int tries = 0; tries < 400 && placed < 46; ++tries) {
            Vec3 p{rng.range(-kHalf + 40, kHalf - 40), 0, rng.range(-kHalf + 40, kHalf - 40)};
            float dp = pathDist(p);
            if (dp < 5 || dp > 45 || std::fabs(p.z - riverZ(p.x)) < 8 || length(flat(p - village)) < 35) continue;
            Pickup it;
            it.placed = it.resting = true;
            it.seed = rng.range(0, 10);
            it.pos = {p.x, ground(p.x, p.z) + 0.25f, p.z};
            if (placed % 4 == 3) {  // un puñado de lúmenes
                it.kind = PickupKind::Munny;
                for (int k = 0; k < 3; ++k) {
                    it.value = 10;
                    pickups_.push_back(it);
                    it.pos += Vec3{0.5f, 0, 0.3f};
                    it.pos.y = ground(it.pos.x, it.pos.z) + 0.25f;
                }
            } else {
                it.kind = PickupKind::Item;
                it.item = loose[rng.pick(10)];
                it.value = 1;
                pickups_.push_back(it);
            }
            clearings.push_back({p, 2.5f});
            ++placed;
        }
    }

    // Bosques: el pinar del norte (sube por las laderas de la sierra), el robledal del oeste y
    // el del suroeste, con claros donde el ruido baja; y pinos sueltos en las laderas altas.
    auto forest = [&](float x, float z) {  // densidad del bosque en [0, 1]
        float d = 0;
        d = std::max(d, smoothFalloff(length(flat(Vec3{x, 0, z} - Vec3{140, 0, 160})), 115.0f));  // pinar
        d = std::max(d, smoothFalloff(length(flat(Vec3{x, 0, z} - Vec3{-150, 0, 40})), 90.0f));   // bosque del oeste
        d = std::max(d, smoothFalloff(length(flat(Vec3{x, 0, z} - Vec3{-120, 0, -130})), 85.0f)); // robledal
        return d * clampf(0.55f + noise(x / 26.0f, z / 26.0f, 31), 0, 1);  // claros
    };
    for (int k = 0; k < 5200; ++k) {
        Vec3 p{rng.range(-kHalf + 30, kHalf - 30), 0, rng.range(-kHalf + 30, kHalf - 30)};
        float dens = forest(p.x, p.z);
        if (rng.next() > dens * 0.85f || !clearOf(p, 4, 10)) continue;
        float h = ground(p.x, p.z);
        if (h > 52) continue;  // por encima, roca pelada
        bool pines = (p.z > 60 && p.x > 20) || rng.next() < (h > 20 ? 0.8f : 0.15f);
        if (pines) addProp(rng.next() < 0.4f ? "pino_alto" : "pino", p, rng.range(0, 6.28f), rng.range(0.85f, 1.3f));
        else addProp(rng.next() < 0.3f ? "roble_grande" : "roble", p, rng.range(0, 6.28f), rng.range(0.85f, 1.25f));
        float r = rng.next();
        if (r < 0.25f) addProp("arbusto", p + Vec3{rng.range(-4, 4), 0, rng.range(2, 5)}, rng.range(0, 6.28f));
        else if (r < 0.32f) addProp(rng.next() < 0.5f ? "tocon" : "tronco_caido", p + Vec3{rng.range(-5, 5), 0, rng.range(-5, 5)}, rng.range(0, 6.28f));
    }
    for (int k = 0; k < 500; ++k) {  // pinos en las laderas altas, fuera de los bosques
        Vec3 p{rng.range(-kHalf + 30, kHalf - 30), 0, rng.range(-kHalf + 30, kHalf - 30)};
        float h = ground(p.x, p.z);
        if (h < 16 || h > 50 || forest(p.x, p.z) > 0.2f || !clearOf(p, 4, 10)) continue;
        addProp(rng.next() < 0.5f ? "pino_alto" : "pino", p, rng.range(0, 6.28f), rng.range(0.8f, 1.2f));
    }
    // Robles por los prados, arbustos, rocas, tocones y troncos.
    for (int k = 0; k < 140; ++k) {
        Vec3 p{rng.range(-170, 170), 0, rng.range(-170, 170)};
        if (!clearOf(p, 6, 11) || forest(p.x, p.z) > 0.3f) continue;
        addProp(rng.next() < 0.3f ? "roble_grande" : "roble", p, rng.range(0, 6.28f), rng.range(0.85f, 1.2f));
        if (rng.next() < 0.5f) addProp("arbusto", p + Vec3{rng.range(-4, 4), 0, rng.range(2, 5)}, rng.range(0, 6.28f));
    }
    for (int k = 0; k < 260; ++k) {
        Vec3 p{rng.range(-kHalf + 30, kHalf - 30), 0, rng.range(-kHalf + 30, kHalf - 30)};
        if (!clearOf(p, 3, 8)) continue;
        float r = rng.next();
        const char* what = r < 0.35f ? "arbusto" : r < 0.5f ? "arbusto_flores" : r < 0.72f ? "piedra" : r < 0.86f ? "roca"
                         : r < 0.93f ? "tocon" : "tronco_caido";
        addProp(what, p, rng.range(0, 6.28f), rng.range(0.8f, 1.3f));
    }
    for (int k = 0; k < 120; ++k) {  // rocas grandes en las laderas y las cumbres
        Vec3 p{rng.range(-kHalf + 30, kHalf - 30), 0, rng.range(-kHalf + 30, kHalf - 30)};
        if (!clearOf(p, 7, 10) || ground(p.x, p.z) < 8) continue;
        addProp(rng.next() < 0.6f ? "roca_grande" : "roca", p, rng.range(0, 6.28f), rng.range(0.9f, 1.9f));
    }
    // Hierba alta y flores (solo se dibujan cerca).
    for (int k = 0; k < 4200; ++k) {
        Vec3 p{rng.range(-kHalf + 40, kHalf - 40), 0, rng.range(-kHalf + 40, kHalf - 40)};
        if (pathDist(p) < 2.5f || std::fabs(p.z - riverZ(p.x)) < 7 || ground(p.x, p.z) > 30) continue;
        float r = rng.next();
        addProp(r < 0.8f ? "hierba" : r < 0.9f ? "flores_amarillas" : "flores_lila", p, rng.range(0, 6.28f), rng.range(0.8f, 1.4f));
    }
    wildSafe_ = {village, road.front()};  // ni en el pueblo ni en la llegada
    // Habitantes: la granja y la loma.
    addNpc("granjera", village + Vec3{22, 0, 11}, 4);
    addNpc("granjera", village + Vec3{-6, 0, -4}, 9);
    addNpc("aceitero", village + Vec3{2, 0, 5}, 4);
    addNpc("trapera", {30, 0, -22}, 5);
    addNpc("granjera", hill + Vec3{8, 0, -12}, 12);
    addNpc("relojero", hill + Vec3{-6, 0, -16}, 6);
    // Dos portales para cambiar de luz a sombra.
    for (Vec3 p : portalSpots) addPortal({p.x, ground(p.x, p.z), p.z});

    heroStart_ = {road.front().x, ground(road.front().x, road.front().z), road.front().z};
    heroStartYaw_ = 0;
    // La Gruta de las Peñas (otra zona, levels_gruta.cpp): su boca en la peña, al final del
    // camino del este, con rocas grandes que hacen de ladera; y, unos pasos más allá, la grieta
    // por la que se sale del fondo de la gruta. Las llegadas (arrival::kCampo…): el principio,
    // delante de la boca y delante de la grieta.
    arrivals_.push_back({heroStart_, heroStartYaw_});
    {
        Vec3 end = toCrags.back();
        Vec3 mouth = end + Vec3{3, 0, 6}, crack = end + Vec3{-7, 0, 7};
        for (auto [p, prop, to, arrival, label] :
             {std::tuple<Vec3, const char*, LevelId, int, const char*>{mouth, "boca_gruta", LevelId::Gruta, arrival::kGrutaMouth, "Entrar · Gruta de las Peñas"},
              {crack, "grieta_gruta", LevelId::Gruta, arrival::kGrutaGrieta, "Entrar · por la grieta"}}) {
            // De cara al camino, a 90° justos (las cajas de choque de la pieza, sin girar de más).
            Vec3 toEnd = flat(end - p);
            Vec3 out = std::fabs(toEnd.x) > std::fabs(toEnd.z) ? Vec3{toEnd.x > 0 ? 1.0f : -1.0f, 0, 0} : Vec3{0, 0, toEnd.z > 0 ? 1.0f : -1.0f};
            float yaw = std::atan2(out.x, out.z);
            addProp(prop, p, yaw);
            for (int k = -2; k <= 2; ++k) {  // la peña alrededor y por detrás
                Vec3 side{-out.z, 0, out.x};
                // (giradas de 90 en 90°: girada de otro modo, su caja de choque crece y taparía la entrada)
                addProp(std::abs(k) == 2 ? "roca" : "roca_grande", p - out * (4.6f + 1.0f * float(std::abs(k))) + side * (float(k) * 3.2f),
                        float(k + 2) * kPi * 0.5f, 1.1f + 0.15f * float(2 - std::abs(k)));
            }
            if (to == LevelId::Gruta && arrival == arrival::kGrutaMouth) {  // un farol y la señal, para que se vea de lejos
                Vec3 side{-out.z, 0, out.x};
                addProp("farol_camino", p + out * 2.2f + side * 2.6f, yaw);
                addProp("senal_camino", p + out * 3.0f - side * 2.8f, yaw + 0.4f);
            }
            Vec3 door = p + out * 1.3f, outside = p + out * 3.0f;
            doors_.push_back({{door.x, ground(door.x, door.z), door.z}, to, arrival, label});
            arrivals_.push_back({{outside.x, ground(outside.x, outside.z), outside.z}, yaw});
        }
    }
    // Nodos de color: devuelven el color al valle.
    const uint32_t nodeColors[] = {rgba(255, 190, 60), rgba(255, 70, 190), rgba(40, 220, 255), rgba(120, 255, 140)};
    for (int k = 0; k < 4; ++k) {
        Vec3 p = nodeSpots[k];
        p.y = ground(p.x, p.z);
        nodes_.push_back({"nodo_campo_" + std::to_string(k), p, 34, nodeColors[k], ""});
    }
    LevelPart explore{"Campo abierto", "Enciende los cuatro nodos del valle", PartKind::Nodes, "", 4, {}, nodeSpots[0], {}, "", ""};
    parts_ = {explore};
    nextLevel_ = LevelId::Campo;
}

// --- Ciudad Neón -------------------------------------------------------------------------

void Game::buildNeon() {
    levelName_ = "Ciudad Neón";
    floorColor_ = rgba(46, 48, 56);
    floorMaterial_ = Material::Ground;
    night_ = 1;
    fogScale_ = 0.55f;
    propScenery_ = false;  // las fachadas, con su sombreado propio: la oscuridad no las apaga
    wildSpawns_ = wildOnlyMoths_ = true;  // de vez en cuando, una nube de polillas va a por las farolas
    viewDistance_ = 300;
    const Mat4 id = Mat4::identity();
    Rng rng{8128};
    const float kBlock = 44, kRoad = 12, kWalk = 3;  // manzana (de eje a eje), calzada y acera
    const int kN = 3;  // manzanas de -kN a kN
    const float half = (float(kN) + 0.5f) * kBlock;
    // Aceras (un escalón bajo) y líneas de la calzada.
    for (int bx = -kN; bx <= kN; ++bx)
        for (int bz = -kN; bz <= kN; ++bz) {
            Vec3 c{float(bx) * kBlock, 0, float(bz) * kBlock};
            float inner = (kBlock - kRoad) * 0.5f;
            addDecor(id, c + Vec3{0, 0.07f, 0}, {inner, 0.07f, inner}, rgba(92, 94, 104), Material::Stone, false);
            addDecor(id, c + Vec3{0, 0.145f, 0}, {inner - kWalk, 0.005f, inner - kWalk}, rgba(60, 62, 70), Material::Ground, false);
        }
    for (int k = -kN; k <= kN + 1; ++k) {  // líneas discontinuas en el centro de cada calle
        float r = (float(k) - 0.5f) * kBlock;
        for (float s = -half; s < half; s += 6) {
            addDecor(id, {r, 0.012f, s}, {0.1f, 0.01f, 1.5f}, rgba(230, 210, 120), Material::Cloth, false);
            addDecor(id, {s, 0.012f, r}, {1.5f, 0.01f, 0.1f}, rgba(230, 210, 120), Material::Cloth, false);
        }
    }
    const char* towers[] = {"torre_neon_a", "torre_neon_b", "torre_neon_c", "torre_neon_d"};
    const char* shops[] = {"tienda_ramen", "tienda_kombini", "tienda_recreativos"};
    const char* signs[] = {"letrero_rosa", "letrero_cian", "letrero_amarillo"};
    for (int bx = -kN; bx <= kN; ++bx)
        for (int bz = -kN; bz <= kN; ++bz) {
            Vec3 c{float(bx) * kBlock, 0, float(bz) * kBlock};
            float inner = (kBlock - kRoad) * 0.5f;
            if (bx == 0 && bz == 0) {  // el santuario: torii, cerezos y puestos de comida
                addProp("torii", c + Vec3{0, 0, inner - 4}, 0);
                addProp("torii", c + Vec3{0, 0, inner - 10}, 0);
                for (int k = 0; k < 6; ++k) {
                    float a = float(k) * kPi / 3 + 0.4f;
                    addProp("cerezo", c + Vec3{std::cos(a) * 10, 0, std::sin(a) * 9 - 3}, a, rng.range(0.9f, 1.2f));
                }
                for (int k = 0; k < 3; ++k) addProp("puesto_comida", c + Vec3{-8.0f + float(k) * 8, 0, -inner + 4}, kPi);
                addProp("farola_led", c + Vec3{-5, 0, 2}, 0);
                addProp("farola_led", c + Vec3{5, 0, 2}, kPi);
                continue;
            }
            // Torres al fondo de la manzana; tiendas, letreros y expendedoras en el borde que da a la calle.
            int towersHere = 1 + rng.pick(3);
            for (int k = 0; k < towersHere; ++k) {
                float ox = (k == 0 ? -1.0f : k == 1 ? 1.0f : 0.0f) * 9.0f, oz = k == 2 ? -9.0f : 2.0f;
                addProp(towers[rng.pick(4)], c + Vec3{ox, 0.14f, oz}, float(rng.pick(4)) * kPi * 0.5f, 1.0f, false);
            }
            for (int side = 0; side < 4; ++side) {
                float yaw = float(side) * kPi * 0.5f;
                Vec3 out{std::sin(yaw), 0, std::cos(yaw)}, along{out.z, 0, -out.x};
                Vec3 edge = c + out * (inner - kWalk - 4.5f);
                if (rng.next() < 0.6f) addProp(shops[rng.pick(3)], edge + along * rng.range(-8, 8), yaw, 1.0f, false);
                Vec3 walk = c + out * (inner - 1.2f);
                if (side < 2)  // una acera de cada calle (la de enfrente es de la manzana vecina), al tresbolillo
                    for (float s = -inner + 6 + float(side) * 8; s < inner - 3; s += 16)
                        addProp("farola_led", walk + along * s + Vec3{0, 0.14f, 0}, yaw + kPi, 1.0f, false);
                if (rng.next() < 0.5f) addProp(signs[rng.pick(3)], walk + along * rng.range(-10, 10) - out * 1.5f + Vec3{0, 0.14f, 0}, yaw, 1.0f, false);
                if (rng.next() < 0.5f)
                    addProp(rng.next() < 0.5f ? "maquina_expendedora" : "maquina_expendedora_roja",
                            walk + along * rng.range(-12, 12) - out * 1.8f + Vec3{0, 0.14f, 0}, yaw, 1.0f, false);
                if (rng.next() < 0.3f) addProp("poste_electrico", walk + along * rng.range(-14, 14) + Vec3{0, 0.14f, 0}, yaw, 1.0f, false);
            }
            if (rng.next() < 0.25f) addProp("pantalla_gigante", c + Vec3{0, 0.14f, -inner + 6}, kPi, 1.0f, false);
        }
    // Cruces: semáforos y pasos de cebra; coches y taxis aparcados.
    for (int ix = -kN; ix <= kN + 1; ++ix)
        for (int iz = -kN; iz <= kN + 1; ++iz) {
            Vec3 x{(float(ix) - 0.5f) * kBlock, 0, (float(iz) - 0.5f) * kBlock};
            if (std::fabs(x.x) > half - 1 || std::fabs(x.z) > half - 1) continue;
            addProp("paso_cebra", x + Vec3{0, 0, kRoad * 0.5f + 2.5f}, kPi * 0.5f, 1.0f, false);
            addProp("paso_cebra", x + Vec3{kRoad * 0.5f + 2.5f, 0, 0}, 0, 1.0f, false);
            addProp("semaforo", x + Vec3{kRoad * 0.5f + 1, 0.14f, kRoad * 0.5f + 1}, kPi, 1.0f, false);
            addProp("semaforo", x + Vec3{-kRoad * 0.5f - 1, 0.14f, -kRoad * 0.5f - 1}, 0, 1.0f, false);
            for (int k = 0; k < 2; ++k)
                if (rng.next() < 0.55f) {
                    bool alongZ = k == 0;
                    Vec3 p = x + (alongZ ? Vec3{kRoad * 0.5f - 2.0f, 0, rng.range(12, 30)} : Vec3{rng.range(12, 30), 0, kRoad * 0.5f - 2.0f});
                    addProp(rng.next() < 0.4f ? "taxi" : "coche", p, alongZ ? 0.0f : kPi * 0.5f, 1.0f, false);
                }
        }
    // Vía elevada a lo largo de una avenida.
    for (float z = -half + 10; z < half - 5; z += 20) addProp("via_elevada", {kBlock * 0.5f, 0, z}, 0, 1.0f, false);
    // Cierre del mapa: torres altas en todo el borde.
    for (float s = -half; s <= half; s += 14) {
        for (Vec3 p : {Vec3{s, 0, half + 8}, Vec3{s, 0, -half - 8}, Vec3{half + 8, 0, s}, Vec3{-half - 8, 0, s}})
            addProp(towers[rng.pick(4)], p, float(rng.pick(4)) * kPi * 0.5f, 1.0f, false);
    }
    // Habitantes por las aceras y en el santuario.
    for (int i = 0; i < 22; ++i) {
        int bx = rng.pick(2 * kN + 1) - kN, bz = rng.pick(2 * kN + 1) - kN, side = rng.pick(4);
        float yaw = float(side) * kPi * 0.5f;
        Vec3 out{std::sin(yaw), 0, std::cos(yaw)}, along{out.z, 0, -out.x};
        Vec3 p = Vec3{float(bx) * kBlock, 0, float(bz) * kBlock} + out * ((kBlock - kRoad) * 0.5f - 1.5f) + along * rng.range(-12, 12);
        addNpc(i % 3 == 0 ? "nino_sombras" : i % 3 == 1 ? "oficinista" : "vidriera", p, 9);
    }
    for (int k = 0; k < 3; ++k) addNpc("cocinero", {-8.0f + float(k) * 8, 0, -(kBlock - kRoad) * 0.5f + 6.5f}, 1.5f);
    addNpc("musica", {3, 0, 6}, 2);
    addNpc("oficinista", {-4, 0, 9}, 6);
    heroStart_ = {-kBlock * 0.5f, 0, -12};
    heroStartYaw_ = 0;
    addPortal({-kBlock * 0.5f + 3, 0, 6});
    addPortal({kBlock * 1.5f, 0, kBlock * 0.5f});
    LevelPart explore{"Ciudad Neón", "Llega al santuario del centro", PartKind::Reach, "", 0, {{-12, -1, -12}, {12, 10, 12}},
                      {0, 2, 0}, {}, "", ""};
    parts_ = {explore};
    nextLevel_ = LevelId::Neon;
}

// --- Pueblo cafetero ---------------------------------------------------------------------

void Game::buildCafetero() {
    levelName_ = "Pueblo cafetero";
    floorColor_ = rgba(78, 138, 62);
    floorMaterial_ = Material::Grass;
    night_ = 0.45f;  // atardecer detenido
    fogScale_ = 0.3f;
    darkScale_ = 0.6f;
    viewDistance_ = 380;
    const float kSize = 320;
    makeTerrain(kSize, 2.0f);
    Terrain& t = const_cast<Terrain&>(*world_.terrain);
    Rng rng{57};
    // El pueblo está en lo alto de una loma: la plaza en el centro, calles en cruz y en anillo.
    const float kPlaza = 22;  // medio lado de la plaza
    const float plazaY = 12;
    auto streetAt = [&](Vec3 p) {  // distancia a la calle más cercana
        float d = 1e9f;
        for (float c : {-48.0f, 0.0f, 48.0f}) {
            if (std::fabs(p.z) < 80) d = std::min(d, std::fabs(p.x - c));
            if (std::fabs(p.x) < 80) d = std::min(d, std::fabs(p.z - c));
        }
        return d;
    };
    for (int iz = 0; iz < t.nz; ++iz)
        for (int ix = 0; ix < t.nx; ++ix) {
            float x = t.x0 + float(ix) * t.cell, z = t.z0 + float(iz) * t.cell;
            Vec3 p{x, 0, z};
            float r = length(flat(p));
            // Loma: el pueblo arriba, laderas de cafetal y una cañada al este con guaduas.
            float h = plazaY * std::exp(-r * r / (2 * 95.0f * 95.0f)) * 1.25f + 5.0f * (fbm(x / 60.0f, z / 60.0f, 33) + 0.6f);
            h -= 9.0f * std::exp(-((x - 110) * (x - 110)) / (2 * 14.0f * 14.0f)) * smoothFalloff(std::fabs(z), 150);
            float edge = std::min({x - t.x0, -t.x0 - x, z - t.z0, -t.z0 - z});
            if (edge < 40) h += 30.0f * (1 - edge / 40) * (1 - edge / 40);
            // La plaza, llana; las calles del pueblo bajan suaves.
            float town = smoothFalloff(r, 85);
            float townH = plazaY + 1.5f - 0.06f * r;
            h = h + (townH - h) * town;
            float plazaD = std::max(std::fabs(x), std::fabs(z)) - kPlaza;
            h = h + (plazaY - h) * smoothFalloff(std::max(plazaD, 0.0f), 8.0f);
            t.at(ix, iz) = h;
            float v = noise(x / 6.0f, z / 6.0f, 9) * 0.5f + 0.5f;
            uint32_t col = mixRgb(rgba(66, 128, 52), rgba(104, 160, 62), v);  // verde del trópico
            Material m = Material::Grass;
            float cob = noise(x * 1.3f, z * 1.3f, 4) * 0.5f + 0.5f;  // empedrado: manchas de piedra
            if (plazaD < 0) col = mixRgb(rgba(156, 146, 130), rgba(124, 114, 102), cob), m = Material::Stone;  // plaza empedrada
            else if (streetAt(p) < 4.0f && r < 82) col = mixRgb(rgba(134, 122, 106), rgba(106, 98, 86), cob), m = Material::Stone;
            else if (r > 95 && r < 150 && int((x + z) / 3.5f) % 2 == 0) col = mixRgb(col, rgba(150, 82, 52), 0.35f);  // surcos de tierra roja
            paintTerrain(ix, iz, col, m);
        }
    auto ground = [&](float x, float z) { return t.heightAt(x, z); };

    // Plaza: iglesia al norte, kiosco en el centro, bancas, faroles, yipaos y palmas.
    addProp("iglesia", {0, 0, kPlaza + 12}, kPi);
    addProp("kiosco", {0, 0, 0});
    for (int k = 0; k < 8; ++k) {
        float a = float(k) * kPi / 4 + kPi / 8;
        addProp("banca_parque", {std::cos(a) * 9, 0, std::sin(a) * 9}, -a - kPi * 0.5f);
        addProp("heliconia", {std::cos(a) * 12, 0, std::sin(a) * 12}, a);
    }
    for (int k = 0; k < 4; ++k) {
        float a = float(k) * kPi / 2 + kPi / 4;
        addProp("palma_de_cera", {std::cos(a) * 15, 0, std::sin(a) * 15}, a, 0.7f);
    }
    for (float s = -kPlaza + 2; s <= kPlaza - 2; s += 11)
        for (Vec3 p : {Vec3{s, 0, kPlaza - 1}, Vec3{s, 0, -kPlaza + 1}, Vec3{kPlaza - 1, 0, s}, Vec3{-kPlaza + 1, 0, s}})
            addProp("farol_colonial", p);
    addProp("yipao_rojo", {-kPlaza + 4, 0, -10}, 0.2f);
    addProp("yipao_azul", {-kPlaza + 4, 0, -4}, 0.1f);
    addProp("yipao_rojo", {kPlaza - 4, 0, 9}, kPi - 0.15f);
    // Casas de colores a los dos lados de las calles, con la fachada hacia la calle.
    const char* houses[] = {"casa_cafetera_rojo",    "casa_cafetera_azul",     "casa_cafetera_verde",     "casa_cafetera_amarillo",
                            "casa_cafetera_naranja", "casa_cafetera_morado",   "casa_cafetera_turquesa",  "casa_cafetera_baja_verde",
                            "casa_cafetera_baja_rojo", "casa_cafetera_ancha_azul"};
    auto row = [&](Vec3 from, Vec3 dir, float len, float yaw) {
        for (float s = 0; s < len; s += 9.5f) {
            Vec3 p = from + dir * s;
            if (std::max(std::fabs(p.x), std::fabs(p.z)) < kPlaza + 6) continue;  // la plaza queda libre
            addProp(houses[rng.pick(10)], p, yaw);
        }
    };
    // Plaza: casas en tres lados (al norte está la iglesia).
    for (float s = -kPlaza + 2; s < kPlaza; s += 9.5f) {
        addProp(houses[rng.pick(7)], {s, 0, -kPlaza - 8.5f}, 0);
        if (std::fabs(s) > 8) {
            addProp(houses[rng.pick(7)], {kPlaza + 8.5f, 0, s}, -kPi * 0.5f);
            addProp(houses[rng.pick(7)], {-kPlaza - 8.5f, 0, s}, kPi * 0.5f);
        }
    }
    for (float c : {-48.0f, 48.0f}) {  // calles paralelas
        row({-78, 0, c - 8.5f}, {1, 0, 0}, 156, 0);
        row({-78, 0, c + 8.5f}, {1, 0, 0}, 156, kPi);
        row({c - 8.5f, 0, -78}, {0, 0, 1}, 156, kPi * 0.5f);
        row({c + 8.5f, 0, -78}, {0, 0, 1}, 156, -kPi * 0.5f);
    }
    for (Vec3 dir : {Vec3{1, 0, 0}, Vec3{-1, 0, 0}, Vec3{0, 0, -1}}) {  // calles que salen de la plaza
        Vec3 side{dir.z, 0, -dir.x};
        float yaw = std::atan2(side.x, side.z);
        row(dir * (kPlaza + 14) + side * 8.5f, dir, 48, yaw + kPi);
        row(dir * (kPlaza + 14) - side * 8.5f, dir, 48, yaw);
        for (float s = kPlaza + 10; s < 80; s += 16) addProp("farol_colonial", dir * s + side * 3.8f);
    }
    // Alrededor: cafetal con plataneras de sombrío, guadual en la cañada y palmas de cera en los cerros.
    for (int k = 0; k < 900; ++k) {
        float a = rng.range(0, 6.28f), r = rng.range(92, 150);
        Vec3 p{std::cos(a) * r, 0, std::sin(a) * r};
        if (std::fabs(p.x - 110) < 14 && std::fabs(p.z) < 120) continue;
        // En hileras siguiendo la ladera.
        float rr = std::round(r / 2.6f) * 2.6f;
        p = {std::cos(a) * rr, 0, std::sin(a) * rr};
        addProp(rng.next() < 0.88f ? "cafeto" : "platanera", p, rng.range(0, 6.28f), rng.range(0.85f, 1.15f));
    }
    for (int k = 0; k < 40; ++k) addProp("guadual", {rng.range(100, 120), 0, rng.range(-120, 120)}, rng.range(0, 6.28f), rng.range(0.8f, 1.2f));
    for (int k = 0; k < 45; ++k) {
        float a = rng.range(0, 6.28f), r = rng.range(152, 175);
        addProp(rng.next() < 0.4f ? "palma_de_cera_alta" : "palma_de_cera", {std::cos(a) * r, 0, std::sin(a) * r}, rng.range(0, 6.28f));
    }
    for (int k = 0; k < 60; ++k) {
        float a = rng.range(0, 6.28f), r = rng.range(84, 150);
        addProp(rng.next() < 0.5f ? "heliconia" : "platanera", {std::cos(a) * r, 0, std::sin(a) * r}, rng.range(0, 6.28f));
    }
    for (int k = 0; k < 1400; ++k) {
        float a = rng.range(0, 6.28f), r = rng.range(84, 160);
        addProp("hierba", {std::cos(a) * r, 0, std::sin(a) * r}, rng.range(0, 6.28f), rng.range(0.9f, 1.5f));
    }
    // Habitantes: la plaza y el cafetal.
    for (int i = 0; i < 9; ++i) {
        float a = float(i) * 0.7f, r = 6.0f + float(i % 3) * 4.5f;
        const char* who[] = {"recolectora", "aceitero", "pregonero", "relojero", "trapera"};
        addNpc(who[i % 5], {std::cos(a) * r, 0, std::sin(a) * r}, 7);
    }
    for (int i = 0; i < 6; ++i) {
        float a = rng.range(0, 6.28f), r = rng.range(98, 140);
        addNpc(i % 2 ? "recolectora" : "granjera", {std::cos(a) * r, 0, std::sin(a) * r}, 6);
    }
    for (Vec3 p : {Vec3{0, 0, -40}, Vec3{-40, 0, 0}, Vec3{40, 0, 0}}) addNpc("deshollinadora", p, 10);
    addPortal({-8, ground(-8, -14), -14});
    addPortal({60, ground(60, 0), 0});
    heroStart_ = {0, ground(0, -12), -12};
    heroStartYaw_ = 0;
    // Nodos de color: la plaza, el atrio de la iglesia, el cafetal y el guadual.
    const Vec3 nodeSpots[] = {{12, 0, -12}, {-9, 0, kPlaza - 3}, {-118, 0, -20}, {104, 0, 26}};
    const uint32_t nodeColors[] = {rgba(255, 190, 60), rgba(40, 220, 255), rgba(255, 70, 190), rgba(120, 255, 140)};
    for (int k = 0; k < 4; ++k) {
        Vec3 p = nodeSpots[k];
        p.y = ground(p.x, p.z);
        nodes_.push_back({"nodo_pueblo_" + std::to_string(k), p, 32, nodeColors[k], ""});
    }
    LevelPart explore{"Pueblo cafetero", "Enciende los cuatro nodos del pueblo", PartKind::Nodes, "", 4, {}, nodeSpots[0], {}, "", ""};
    parts_ = {explore};
    nextLevel_ = LevelId::Cafetero;
}

// Sombra de día: algo tapa el sol (rayo hacia el sol contra las cajas del mundo y el relieve).
bool Game::sunBlocked(Vec3 p) const {
    RayHit h;
    return segmentVsWorld(p + Vec3{0, 0.3f, 0}, p + Vec3{0, 0.3f, 0} + kSun * 40.0f, world_, h);
}
