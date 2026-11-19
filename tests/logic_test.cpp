// Test de la lógica del juego, sin ventana ni OpenGL: se rellena `Controls` a mano, se
// simulan pasos fijos de 1/60 s (determinista) y se comprueba el estado interno.
// Se compila con AddressSanitizer + UBSan. Ejecutar: ctest --test-dir build  (o build/logic_test)
#define private public  // solo para el test: acceso al estado interno
#include "game/game.h"
#undef private
#include "actors/hero_clips.h"
#include "audio/barks.h"
#include "audio/synth.h"
#include "game/props.h"
#include "render/glb.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <memory>

const float dt = 1.0f / 60;
const float kBruteHp = enemyDef(EnemyKind::Brute).maxHealth;
int fails = 0;
#define CHECK(c, ...)                                   \
    do {                                                \
        bool ok_ = (c);                                 \
        std::printf("%s ", ok_ ? "[OK]  " : "[FAIL]");  \
        std::printf(__VA_ARGS__);                       \
        std::printf("\n");                              \
        if (!ok_) ++fails;                              \
    } while (0)

// Simula `secs` segundos. Los flancos de `c` solo valen en el primer paso.
void run(Game& g, float secs, Controls c = {}) {
    for (int i = 0; i < int(secs * 60); ++i) {
        g.update(dt, c);
        c.jump = c.attack = c.dodge = c.interact = c.lockOn = c.nextTarget = c.unlock = c.runCommand = c.light = c.transform = c.summon = c.swapMode = false;
        c.command = -1;
        c.commandScroll = 0;
    }
}
Controls press(bool Controls::*flag) {
    Controls c;
    c.*flag = true;
    return c;
}
// Ejecutar la entrada `i` del menú de comandos (técnicas 0-4 de la mitad activa).
Controls command(int i) {
    Controls c;
    c.command = i;
    return c;
}

// Partida sin guion (sin combates de zona ni escenas): cada test coloca sus enemigos.
// `place` pone al jugador ahí y le deja posarse en el suelo.
Game fresh() {
    Game g;
    g.init();
    return g;
}
// Pasa todas las escenas que detienen el juego.
void skipDialogues(Game& g) {
    Controls adv;
    adv.advance = true;
    for (int i = 0; i < 2000 && (g.dialogueBlocking() || !g.sceneQueue_.empty()); ++i) {
        g.update(dt, adv);
        g.update(dt, {});
    }
}
void place(Game& g, Vec3 pos, float yaw = 0) {
    g.hero_.body.pos = pos;
    g.hero_.yaw = yaw;
    for (int i = 0; i < 6; ++i) g.update(dt, {});
}
Enemy& addEnemy(Game& g, EnemyKind k, Vec3 pos, AiState state = AiState::Idle) {
    Enemy e = makeEnemy(k, pos, kPi, 1.0f);
    e.state = state;
    if (e.def().flying) e.body.pos.y = e.def().hoverHeight - e.def().height * 0.5f;
    g.enemies_.push_back(e);
    return g.enemies_.back();
}

// Matriz mundo de un nodo del .glb en el instante `t` de una animación (independiente de
// hero_clips.cpp: lo que el juego tiene que reproducir).
Mat4 glbWorldAt(const GlbFile& f, const GlbAnimation& a, int node, float t) {
    if (node < 0) return Mat4::identity();
    Mat4 local = f.nodes[size_t(node)].local;
    Vec3 tr = translationOf(local);
    Mat4 rot = local;
    rot.m[12] = rot.m[13] = rot.m[14] = 0;
    for (const GlbChannel& ch : a.channels) {
        if (ch.node != node) continue;
        size_t i = 0;
        while (i + 1 < ch.times.size() && ch.times[i + 1] <= t) ++i;
        size_t j = std::min(i + 1, ch.times.size() - 1);
        float u = j > i ? clampf((t - ch.times[i]) / (ch.times[j] - ch.times[i]), 0, 1) : 0.0f;
        int n = ch.path == GlbChannel::Rotation ? 4 : 3;
        float v[4], d = 0;
        for (int k = 0; k < n; ++k) d += ch.values[i * n + k] * ch.values[j * n + k];
        for (int k = 0; k < n; ++k) v[k] = ch.values[i * n + k] + ((n == 4 && d < 0 ? -1 : 1) * ch.values[j * n + k] - ch.values[i * n + k]) * u;
        if (ch.path == GlbChannel::Translation) tr = {v[0], v[1], v[2]};
        if (ch.path == GlbChannel::Rotation) {
            float l = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2] + v[3] * v[3]);
            float x = v[0] / l, y = v[1] / l, z = v[2] / l, w = v[3] / l;
            rot = Mat4::identity();
            rot.m[0] = 1 - 2 * (y * y + z * z), rot.m[1] = 2 * (x * y + z * w), rot.m[2] = 2 * (x * z - y * w);
            rot.m[4] = 2 * (x * y - z * w), rot.m[5] = 1 - 2 * (x * x + z * z), rot.m[6] = 2 * (y * z + x * w);
            rot.m[8] = 2 * (x * z + y * w), rot.m[9] = 2 * (y * z - x * w), rot.m[10] = 1 - 2 * (x * x + y * y);
        }
    }
    return glbWorldAt(f, a, f.nodes[size_t(node)].parent, t) * translate(tr) * rot;
}

int main() {
    {  // Animaciones de Vela (vela_animaciones.glb): pasadas al esqueleto del juego, las manos,
        // los pies y la cabeza quedan donde los pone el .glb en cada instante de cada clip
        const std::string path = std::string(GAME_ASSET_DIR) + "/models/vela_animaciones.glb";
        std::string err;
        bool ok = loadHeroClips(path, err);
        CHECK(ok, "vela_animaciones.glb: cinco clips leídos %s", err.c_str());
        GlbFile f;
        f.load(path, err);
        const char* names[] = {"correr", "caminar", "saltar", "combo"};
        const std::pair<int, const char*> ends[] = {{WristR, "Hand_R"}, {WristL, "Hand_L"}, {AnkleL, "Foot_L"},
                                                    {AnkleR, "Foot_R"}, {Head, "Head"}, {ElbowR, "LowerArm_R"}};
        float worst = 0;
        for (int c = 0; ok && c < 4; ++c) {
            const GlbAnimation* a = nullptr;
            for (const GlbAnimation& x : f.animations)
                if (x.name == names[c]) a = &x;
            for (float t = 0; a && t < a->duration; t += 0.037f) {
                Mat4 w[JointCount];
                computeWorldMatrices(sampleHeroClip(HeroClip(c), t).pose, Mat4::identity(), w);
                for (auto [joint, node] : ends)
                    worst = std::max(worst, length(translationOf(w[joint]) - translationOf(glbWorldAt(f, *a, f.find(node), t))));
            }
        }
        CHECK(ok && worst < 0.01f, "animaciones de Vela: manos, pies y cabeza donde los pone el .glb (error máximo %.1f mm)", worst * 1000);
        float spin = sampleHeroClip(HeroClip::Roll, 0.9f).spin;
        CHECK(std::fabs(spin - 2 * kPi) < 0.05f, "voltereta: la cadera da una vuelta entera (%.2f rad)", spin);
        // Con los clips, la voltereta del juego acaba de pie y el combo de la hoja sigue
        // golpeando a su ritmo (los clips no cambian los tiempos).
        Character h;
        World flat;
        flat.solids.push_back(boxFromCenter({0, -0.5f, 0}, {50, 0.5f, 50}));
        for (int i = 0; i < 30; ++i) h.update(dt, {}, false, flat);
        h.dodge({0, 0, 1});
        for (int i = 0; i < 40; ++i) h.update(dt, {}, false, flat);
        Mat4 w[JointCount];
        computeWorldMatrices(h.pose, h.modelMatrix(), w);
        float headY = translationOf(w[Head]).y - h.body.pos.y;
        CHECK(!h.rolling() && headY > 1.4f, "con los clips, tras la voltereta queda de pie (cabeza a %.2f m)", headY);
        int impacts = 0;
        for (int i = 0; i < 240; ++i) {
            if (i % 12 == 0) h.attack(0, 0);
            h.update(dt, {}, false, flat);
            impacts += h.takeImpact() != Move::None;
        }
        CHECK(impacts >= 4, "con los clips, el combo de la hoja encadena sus golpes (%d impactos en 4 s)", impacts);
    }
    {  // Terreno con relieve: se baja una cuesta andando sin despegarse, y la rejilla de cajas
        // encuentra lo mismo que mirar todas
        auto t = std::make_shared<Terrain>();
        t->x0 = -50, t->z0 = -50, t->cell = 2, t->nx = t->nz = 51;
        t->heights.resize(51 * 51);
        for (int z = 0; z < 51; ++z)
            for (int x = 0; x < 51; ++x) t->at(x, z) = 0.35f * float(50 - x) * 2.0f;  // baja hacia +x (35 %)
        World w;
        w.terrain = t;
        Body b;
        b.pos = {-20, t->heightAt(-20, 0), 0};
        b.grounded = true;
        int airborne = 0;
        for (int i = 0; i < 120; ++i) {
            b.vel.x = 6.5f, b.vel.z = 0;
            moveBody(b, w, dt, 0.3f);
            airborne += !b.grounded;
        }
        CHECK(airborne == 0 && std::fabs(b.pos.y - t->heightAt(b.pos.x, b.pos.z)) < 1e-3f,
              "terreno: cuesta abajo siempre en el suelo (%d pasos en el aire, x=%.1f)", airborne, b.pos.x);
        RayHit h;
        bool hit = segmentVsWorld({0, 40, 0}, {0, -10, 0}, w, h);
        float y = 40 - 50 * h.t;
        CHECK(hit && std::fabs(y - t->heightAt(0, 0)) < 0.01f, "terreno: el rayo vertical da en el suelo (y=%.2f, suelo %.2f)", y,
              t->heightAt(0, 0));
        World boxes;
        uint32_t seed = 7;
        auto rnd = [&]() { return float((seed = seed * 1664525u + 1013904223u) >> 8) / 16777216.0f; };
        for (int i = 0; i < 400; ++i) {
            Vec3 c{rnd() * 300 - 150, 1, rnd() * 300 - 150}, half{0.3f + rnd() * 6, 1, 0.3f + rnd() * 6};
            boxes.solids.push_back(boxFromCenter(c, half));
        }
        boxes.buildGrid();
        int missing = 0;
        std::vector<int> found;
        for (int q = 0; q < 200; ++q) {
            Vec3 lo{rnd() * 300 - 150, 0, rnd() * 300 - 150}, hi = lo + Vec3{rnd() * 10, 0, rnd() * 10};
            boxes.query(lo, hi, found);
            for (size_t i = 0; i < boxes.solids.size(); ++i) {
                const AABB& a = boxes.solids[i];
                bool touches = a.min.x <= hi.x && a.max.x >= lo.x && a.min.z <= hi.z && a.max.z >= lo.z;
                if (touches && std::find(found.begin(), found.end(), int(i)) == found.end()) ++missing;
            }
        }
        CHECK(!boxes.grid.cells.empty() && missing == 0, "rejilla de cajas: no se pierde ninguna (%d perdidas)", missing);
    }
    {  // Mapas grandes: cargan con sus piezas y su relieve, se anda sobre el terreno, las piezas
        // con COL_ chocan y las de LUZ_ son farolas del juego
        int pieces = propLibrary().loadDir(std::string(GAME_ASSET_DIR) + "/props");
        CHECK(pieces >= 60, "piezas del escenario: %d leídas de assets/props", pieces);
        int casa = propLibrary().find("casa_campo");
        CHECK(casa >= 0 && !propLibrary()[size_t(casa)].colliders.empty() && !propLibrary()[size_t(casa)].lights.empty(),
              "casa_campo: con caja de colisión y farol");
        for (LevelId id : {LevelId::Campo, LevelId::Neon, LevelId::Cafetero, LevelId::Rio}) {
            Game g = fresh();
            g.loadLevel(id);
            run(g, 1.0f);
            const Body& b = g.hero_.body;
            float floor = g.world_.groundAt(b.pos.x, b.pos.z);
            size_t propLamps = 0;
            for (const Lamp& l : g.lamps_) propLamps += l.style == LampStyle::Prop;
            CHECK(g.props_.size() > 150 && b.grounded && std::fabs(b.pos.y - floor) < 0.05f && propLamps > 5,
                  "%s: %zu piezas, %zu farolas de piezas, la protagonista en el suelo (y=%.2f, suelo %.2f)", g.levelName_.c_str(),
                  g.props_.size(), propLamps, b.pos.y, floor);
        }
        {  // Campo: cápsulas en el suelo y objetos sueltos por el mapa que no caducan
            Game g = fresh();
            g.loadLevel(LevelId::Campo);
            int grounded = 0;
            for (const TreasureChest& c : g.chests_) grounded += std::fabs(c.pos.y - g.world_.groundAt(c.pos.x, c.pos.z)) < 0.05f;
            auto placed = [&] {
                size_t n = 0;
                for (const Pickup& p : g.pickups_) n += p.placed;
                return n;
            };
            size_t before = placed();
            run(g, 65.0f);
            CHECK(g.chests_.size() >= 8 && grounded == int(g.chests_.size()) && before >= 30 && placed() == before,
                  "campo: %zu cápsulas (%d en el suelo), %zu objetos sueltos y siguen %zu pasado un minuto", g.chests_.size(), grounded,
                  before, placed());
        }
        {  // La bahía es un diorama cerrado: ni el mar ni el borde del mirador dejan salir
            Game r = fresh();
            r.loadLevel(LevelId::Rio);
            const Terrain& t = *r.world_.terrain;
            struct Walk {
                Vec3 from, dir;
                const char* what;
            };
            for (Walk w : {Walk{{0, 0, -22}, {0, 0, -1}, "mar adentro"}, Walk{{-28, 0, -22.5f}, {1, 0, 0}, "del mirador a la playa"},
                           Walk{{0, 0, 40}, {0, 0, 1}, "al norte de la praça"}, Walk{{-23.5f, 0, 5}, {-1, 0, 0}, "fuera de la escalera"}}) {
                place(r, {w.from.x, r.world_.groundAt(w.from.x, w.from.z) + 2.0f, w.from.z});
                Controls c;
                c.move = w.dir;
                run(r, 5.0f, c);
                Vec3 p = r.hero_.body.pos;
                CHECK(t.fenceAt(p.x, p.z) < 0.1f, "bahía: %s se queda dentro del recinto (%.1f, %.1f; fuera %.2f m)", w.what, p.x, p.z,
                      t.fenceAt(p.x, p.z));
            }
        }
        {  // La bahía se recorre andando: del centro a la iglesia, la vuelta por Guimarães, los
            // becos del morro hasta el mirador y la Rua da Praia hasta la Prainha
            Game r = fresh();
            r.loadLevel(LevelId::Rio);
            struct Route {
                const char* what;
                std::vector<Vec3> pts;
            };
            const Route routes[] = {
                {"bajo los arcos a la iglesia", {{0, 0, 5}, {0, 0, 30}, {0, 0, 42}, {14, 0, 46}, {40, 0, 46}, {46, 0, 58}, {36, 0, 68},
                                                 {44, 0, 80}, {40, 0, 90}, {40, 0, 96}}},
                {"de la iglesia por Guimarães a la praça", {{30, 0, 97}, {10, 0, 95}, {-8, 0, 93}, {-14, 0, 91}, {-20, 0, 84},
                                                            {-22, 0, 70}, {-8, 0, 58}, {0, 0, 50}}},
                {"por los becos del morro al mirador", {{-14, 0, 46}, {-40, 0, 46}, {-46, 0, 40}, {-54, 0, 30}, {-62, 0, 20},
                                                        {-58, 0, 8}, {-58, 0, 0}, {-52, 0, -3}, {-42, 0, -10}, {-32, 0, -17}, {-28, 0, -19}}},
                {"por la Rua da Praia a la Prainha", {{8, 0, -4}, {12, 0, -7.5f}, {26, 0, -9.5f}, {42, 0, -12}, {50, 0, -14},
                                                      {57, 0, -13}, {66, 0, -17}, {74, 0, -24}, {80, 0, -31}, {90, 0, -38}}},
            };
            for (const Route& rt : routes) {
                Vec3 a = rt.pts[0];
                place(r, {a.x, r.world_.groundAt(a.x, a.z) + 0.5f, a.z});
                size_t next = 1;
                for (int i = 0; i < 60 * 120 && next < rt.pts.size(); ++i) {
                    Vec3 to = rt.pts[next], p = r.hero_.body.pos;
                    if (length(flat(to - p)) < 1.5f) {
                        ++next;
                        continue;
                    }
                    Controls c;
                    c.move = normalize(flat(to - p));
                    r.update(dt, c);
                }
                Vec3 p = r.hero_.body.pos;
                CHECK(next == rt.pts.size(), "bahía: %s (llega a %zu/%zu, en %.1f, %.1f, %.1f)", rt.what, next, rt.pts.size(), p.x, p.y, p.z);
            }
        }
        {  // La bahía: tiendas que venden con lúmenes y tabernas donde se descansa
            Game r = fresh();
            r.loadLevel(LevelId::Rio);
            int shops = 0, inns = 0;
            for (const Npc& n : r.npcs_) shops += n.role == NpcRole::Shop && !n.stock.empty();
            for (const ZoneDoor& d : r.doors_) inns += d.to == LevelId::Botequim || d.to == LevelId::TabernaLapa;  // se entra en ellas
            Npc* armazem = nullptr;
            for (Npc& n : r.npcs_)
                if (n.name.find("armazém") != std::string::npos) armazem = &n;
            CHECK(shops >= 3 && inns >= 2 && armazem, "bahía: %d tiendas y %d tabernas", shops, inns);
            if (armazem) {
                // Delante del mostrador, F le habla y abre su género.
                place(r, armazem->body.body.pos + Vec3{-1.45f, 0.3f, 0});
                run(r, 0.3f);
                Controls f;
                f.interact = true;
                r.update(dt, f);
                bool opened = r.takeTradeRequest() && r.shopStock().size() == armazem->stock.size();
                r.inv_.munny = 30;
                int before = r.inv_.count(r.shopStock()[0].item);
                std::string msg;
                bool first = r.buy(0, msg), second = r.buy(0, msg);
                CHECK(opened && first && !second && r.inv_.munny == 30 - r.shopStock()[0].price &&
                          r.inv_.count(r.shopStock()[0].item) == before + 1,
                      "bahía: F en el mostrador abre el armazém (%d) y se compra con lúmenes (quedan %d; «%s»)", opened, r.inv_.munny,
                      msg.c_str());
            }
        }
        {  // Las tabernas son zonas: F en la puerta funde, carga el interior (con sus piezas, y suelta
            // las de la bahía) y se aparece dentro; en la barra se descansa; al salir se vuelve a la
            // acera de la taberna con la bahía como se dejó (la cápsula abierta sigue abierta).
            Game r = fresh();
            r.loadLevel(LevelId::Rio);
            int bayPieces = propLibrary().loadedCount();
            size_t chest = 0;
            for (size_t i = 0; i < r.chests_.size(); ++i)
                if (length(flat(r.chests_[i].pos - r.heroStart_)) < length(flat(r.chests_[chest].pos - r.heroStart_))) chest = i;
            if (!r.chests_.empty()) r.chests_[chest].open = true;
            auto door = [&](LevelId to) {
                for (size_t i = 0; i < r.doors_.size(); ++i)
                    if (r.doors_[i].to == to) return int(i);
                return -1;
            };
            auto through = [&](int d) {  // a la puerta, F y esperar a que se abra el negro
                if (d < 0) return false;
                place(r, r.doors_[size_t(d)].pos + Vec3{0, 0.3f, 0});
                run(r, 0.2f);
                Controls f;
                f.interact = true;
                r.update(dt, f);
                bool started = r.changingZone();
                for (int i = 0; i < 60 * 3 && (r.changingZone() || r.rideFade() > 0.01f); ++i) r.update(dt, Controls{});
                return started;
            };
            for (auto [to, home] : {std::pair<LevelId, int>{LevelId::Botequim, arrival::kRioBotequim}, {LevelId::TabernaLapa, arrival::kRioLapa}}) {
                bool in = through(door(to)) && r.level() == to;
                int inside = propLibrary().loadedCount();
                Npc* inn = nullptr;
                for (Npc& n : r.npcs_)
                    if (n.role == NpcRole::Inn) inn = &n;
                bool rested = false;
                if (inn) {  // delante de la barra, F al tabernero
                    Vec3 front = inn->body.body.pos + (inn->body.forward()) * 1.45f;
                    place(r, front + Vec3{0, 0.3f, 0});
                    run(r, 0.3f);
                    r.hp_ = 10;
                    Controls f;
                    f.interact = true;
                    r.update(dt, f);
                    rested = r.hp_ == r.maxHp_;
                }
                Vec3 p = r.hero_.body.pos;
                CHECK(in && inn && rested && inside < bayPieces / 3 && p.y > -0.1f && p.y < 0.6f,
                      "bahía: se entra en %s (%d piezas cargadas, en la bahía %d) y en la barra se descansa (%d)", r.levelName().c_str(), inside,
                      bayPieces, rested);
                bool out = through(door(LevelId::Rio)) && r.level() == LevelId::Rio;
                Vec3 q = r.hero_.body.pos, want = r.arrivals_[size_t(home)].pos;
                CHECK(out && length(flat(q - want)) < 0.5f && (r.chests_.empty() || r.chests_[chest].open) && propLibrary().loadedCount() == bayPieces,
                      "bahía: al salir, en la acera de la taberna (%.1f, %.1f, %.1f) y la bahía como estaba", q.x, q.y, q.z);
            }
        }
        {  // El teleférico: F en el andén sube a la cabina; al llegar arriba se carga la cima (Urca),
            // con la explanada que se pisa; de allí al Pão de Açúcar, a su aguja, y de vuelta abajo
            Game r = fresh();
            r.loadLevel(LevelId::Rio);
            CHECK(r.cables_.size() == 1 && r.cables_[0].toZone == LevelId::Urca, "bahía: el teleférico sube a Urca (%zu líneas)", r.cables_.size());
            {  // andando desde donde se empieza hasta el andén de abajo (camino por la rejilla), y allí F sube
                const Terrain& t = *r.world_.terrain;
                Vec3 start = r.hero_.body.pos, fin = r.cables_[0].stopA;
                auto cellOf = [&](Vec3 p) {
                    return std::pair<int, int>{int(std::lround((p.x - t.x0) / t.cell)), int(std::lround((p.z - t.z0) / t.cell))};
                };
                auto [sx, sz] = cellOf(start);
                auto [gx, gz] = cellOf(fin);
                std::vector<int> from(size_t(t.nx) * size_t(t.nz), -1);
                std::vector<int> queue{sz * t.nx + sx};
                from[size_t(queue[0])] = queue[0];
                for (size_t qi = 0; qi < queue.size() && from[size_t(gz * t.nx + gx)] < 0; ++qi) {
                    int cx = queue[qi] % t.nx, cz = queue[qi] / t.nx;
                    for (auto [dx, dz] : {std::pair<int, int>{1, 0}, {-1, 0}, {0, 1}, {0, -1}}) {
                        int nx = cx + dx, nz = cz + dz;
                        if (nx < 0 || nz < 0 || nx >= t.nx || nz >= t.nz) continue;
                        size_t k = size_t(nz) * size_t(t.nx) + size_t(nx);
                        if (from[k] >= 0 || t.fence[k] > -0.6f) continue;
                        from[k] = queue[qi];
                        queue.push_back(int(k));
                    }
                }
                std::vector<Vec3> path;
                for (int k = gz * t.nx + gx; from[size_t(k)] >= 0 && k != from[size_t(k)]; k = from[size_t(k)])
                    path.push_back({t.x0 + float(k % t.nx) * t.cell, 0, t.z0 + float(k / t.nx) * t.cell});
                std::reverse(path.begin(), path.end());
                path.push_back(fin);
                for (size_t i = 0; i < path.size(); i += (i + 2 < path.size() ? 2 : 1)) {
                    for (int step = 0; step < 60 * 2 && length(flat(path[i] - r.hero_.body.pos)) > 0.5f; ++step) {
                        Controls c;
                        c.move = normalize(flat(path[i] - r.hero_.body.pos)) * 0.7f;
                        r.update(dt, c);
                    }
                }
                Vec3 p = r.hero_.body.pos;
                auto it = r.nearbyInteract();
                CHECK(path.size() > 1 && length(flat(fin - p)) < 1.0f && it.kind == 8,
                      "bahía: se llega andando al andén del teleférico (%zu pasos; en %.1f, %.1f, %.1f; andén en %.1f, %.1f; F: %d)", path.size(), p.x,
                      p.y, p.z, fin.x, fin.z, it.kind);
                int other = 0;  // alrededor del andén, F es siempre el teleférico (no un interruptor de farolas)
                for (int k = 0; k < 8; ++k) {
                    float a = float(k) * 0.785f;
                    place(r, fin + Vec3{std::cos(a) * 1.2f, 0.3f, std::sin(a) * 1.2f});
                    run(r, 0.2f);
                    int kind = r.nearbyInteract().kind;
                    other += kind >= 0 && kind != 8;  // (donde la estación empuja fuera del alcance, nada)
                }
                CHECK(other == 0, "bahía: junto al andén, F sube al teleférico (%d de 8 sitios dan otra cosa)", other);
                place(r, start + Vec3{0, 0.3f, 0});
            }
            auto ride = [&](int line, LevelId to) {
                Vec3 at = r.cables_[size_t(line)].stopA;
                std::string from = r.levelName();
                place(r, at + Vec3{0, 0.3f, 0});
                run(r, 0.3f);
                Controls f;
                f.interact = true;
                r.update(dt, f);
                bool started = r.riding();
                for (int i = 0; i < 60 * 60 && (r.riding() || r.changingZone() || r.rideFade() > 0.01f); ++i) r.update(dt, Controls{});
                run(r, 0.6f);
                Vec3 p = r.hero_.body.pos;
                float g = r.groundHeight(p + Vec3{0, 1, 0});
                CHECK(started && r.level() == to && std::fabs(p.y - g) < 0.1f && r.hero_.body.grounded && r.world_.terrain->fenceAt(p.x, p.z) < 0,
                      "bahía: teleférico de %s a %s (llega a %.1f, %.1f, %.1f)", from.c_str(), r.levelName().c_str(), p.x, p.y, p.z);
            };
            ride(0, LevelId::Urca);
            float urca = r.hero_.body.pos.y;
            CHECK(urca > 20 && r.cables_.size() == 2 && !r.encounters_.empty() && !r.chests_.empty() && r.npcs_.size() >= 2,
                  "urca: la explanada a %.1f m, dos líneas (%zu), la Fortaleza y su cápsula", urca, r.cables_.size());
            {  // por los senderos, de la explanada al mirante y a la Fortaleza, andando y sin caer: el
                // camino se busca por la rejilla del recinto (lo que deja pisar) y se sigue
                const Terrain& t = *r.world_.terrain;
                Vec3 fin = r.encounters_[0].trigger.center();
                auto cellOf = [&](Vec3 p) {
                    return std::pair<int, int>{int(std::lround((p.x - t.x0) / t.cell)), int(std::lround((p.z - t.z0) / t.cell))};
                };
                auto [sx, sz] = cellOf(r.hero_.body.pos);
                auto [gx, gz] = cellOf(fin);
                std::vector<int> from(size_t(t.nx) * size_t(t.nz), -1);
                std::vector<int> queue{sz * t.nx + sx};
                from[size_t(queue[0])] = queue[0];
                for (size_t qi = 0; qi < queue.size() && from[size_t(gz * t.nx + gx)] < 0; ++qi) {
                    int cx = queue[qi] % t.nx, cz = queue[qi] / t.nx;
                    for (auto [dx, dz] : {std::pair<int, int>{1, 0}, {-1, 0}, {0, 1}, {0, -1}}) {
                        int nx = cx + dx, nz = cz + dz;
                        if (nx < 0 || nz < 0 || nx >= t.nx || nz >= t.nz) continue;
                        size_t k = size_t(nz) * size_t(t.nx) + size_t(nx);
                        if (from[k] >= 0 || t.fence[k] > -0.6f) continue;
                        from[k] = queue[qi];
                        queue.push_back(int(k));
                    }
                }
                std::vector<Vec3> path;
                for (int k = gz * t.nx + gx; from[size_t(k)] >= 0 && k != from[size_t(k)]; k = from[size_t(k)])
                    path.push_back({t.x0 + float(k % t.nx) * t.cell, 0, t.z0 + float(k / t.nx) * t.cell});
                std::reverse(path.begin(), path.end());
                float lowest = 1e9f;
                for (size_t i = 0; i < path.size(); i += 2) {
                    for (int step = 0; step < 60 * 2 && length(flat(path[i] - r.hero_.body.pos)) > 0.5f; ++step) {
                        Controls c;
                        c.move = normalize(flat(path[i] - r.hero_.body.pos)) * 0.7f;
                        r.update(dt, c);
                        lowest = std::min(lowest, r.hero_.body.pos.y);
                    }
                }
                Vec3 p = r.hero_.body.pos;
                CHECK(!path.empty() && length(flat(fin - p)) < 2.0f && lowest > 8,
                      "urca: los senderos llevan a la Fortaleza (%zu pasos; en %.1f, %.1f, %.1f; lo más bajo %.1f)", path.size(), p.x, p.y, p.z,
                      lowest);
            }
            ride(1, LevelId::PaoAcucar);
            CHECK(r.hero_.body.pos.y > 50 && r.world_.ladders.size() == 1 && !r.chests_.empty(), "pão: en lo alto (y=%.1f), con la aguja y su escalera",
                  r.hero_.body.pos.y);
            ride(0, LevelId::Urca);
            ride(0, LevelId::Rio);
            Vec3 p = r.hero_.body.pos, want = r.arrivals_[size_t(arrival::kRioCable)].pos;
            CHECK(length(flat(p - want)) < 0.6f, "bahía: de vuelta en el andén del Largo do Bondinho (%.1f, %.1f)", p.x, p.z);
        }
        {  // La Gruta de las Peñas: se entra desde el campo por la boca; las salas están unidas; el
            // puzle de los espejos tiene solución y abre la puerta; las escaleras sacan del foso,
            // suben al Pozo y salen del lago; por la grieta se vuelve al campo, junto a la boca
            Game g = fresh();
            g.loadLevel(LevelId::Campo);
            int doors = 0, mouth = -1;
            for (size_t i = 0; i < g.doors_.size(); ++i)
                if (g.doors_[i].to == LevelId::Gruta) ++doors, mouth = mouth < 0 ? int(i) : mouth;
            CHECK(doors == 2, "campo: la boca de la gruta y su grieta (%d puertas)", doors);
            if (mouth >= 0) {
                place(g, g.doors_[size_t(mouth)].pos + Vec3{0, 0.3f, 0});
                run(g, 0.2f);
                Controls f;
                f.interact = true;
                g.update(dt, f);
                for (int i = 0; i < 60 * 3 && (g.changingZone() || g.rideFade() > 0.01f); ++i) g.update(dt, Controls{});
            }
            run(g, 0.5f);
            Vec3 p = g.hero_.body.pos;
            CHECK(g.level() == LevelId::Gruta && g.hero_.body.grounded && std::fabs(p.y) < 0.5f,
                  "gruta: se entra por la boca (en %.1f, %.1f, %.1f; «%s»)", p.x, p.y, p.z, g.nearbyInteract().label.c_str());
            if (g.level() != LevelId::Gruta) g.loadLevel(LevelId::Gruta), p = g.hero_.body.pos;
            // Las diez salas, unidas por lo que se pisa (el foso se cruza por abajo; el Pozo, por la escalera).
            const Terrain& t = *g.world_.terrain;
            auto reach = [&](Vec3 from, Vec3 to) {
                auto cell = [&](Vec3 q) { return int(std::lround((q.z - t.z0) / t.cell)) * t.nx + int(std::lround((q.x - t.x0) / t.cell)); };
                std::vector<char> seen(size_t(t.nx) * size_t(t.nz), 0);
                std::vector<int> queue{cell(from)};
                seen[size_t(queue[0])] = 1;
                int goal = cell(to);
                for (size_t qi = 0; qi < queue.size(); ++qi) {
                    if (queue[qi] == goal) return true;
                    int cx = queue[qi] % t.nx, cz = queue[qi] / t.nx;
                    for (auto [dx, dz] : {std::pair<int, int>{1, 0}, {-1, 0}, {0, 1}, {0, -1}}) {
                        int nx = cx + dx, nz = cz + dz;
                        if (nx < 0 || nz < 0 || nx >= t.nx || nz >= t.nz) continue;
                        size_t k = size_t(nz) * size_t(t.nx) + size_t(nx);
                        if (seen[k] || t.fence[k] > -0.4f) continue;
                        seen[k] = 1;
                        queue.push_back(int(k));
                    }
                }
                return false;
            };
            CHECK(reach(p, {0, 0, 96}) && reach({0, 0, 109}, {22, 0, 50}) && reach({22, 0, 50}, {10.6f, 0, 40}) && !reach(p, {0, 0, 109}),
                  "gruta: de la boca al Pozo y de su galería de arriba al Corazón y a la grieta (arriba, solo por la escalera)");
            // El puzle: alguna orientación de los dos espejos lleva el haz al receptor y abre la puerta.
            bool solved = false;
            for (int a = 0; a < 8 && !solved; ++a)
                for (int b = 0; b < 8 && !solved; ++b) {
                    g.mirrors_[0].angle = a;
                    g.mirrors_[1].angle = b;
                    g.traceBeams();
                    solved = !g.receptors_.empty() && g.receptors_[0].lit;
                }
            bool open = false;
            for (const Gate& gt : g.gates_)
                if (gt.id == "gruta_galeria") open = !gt.closed;
            CHECK(solved && open, "gruta: los espejos de la Galería llevan el haz al receptor y abren la puerta");
            auto climb = [&](Vec3 base, Vec3 push, float want, const char* what) {
                place(g, base + Vec3{0, 0.2f, 0});
                run(g, 0.3f);
                Controls c;
                c.move = push;
                c.camForward = push;
                float top = -1e9f;
                for (int i = 0; i < 60 * 8; ++i) {
                    g.update(dt, c);
                    top = std::max(top, g.hero_.body.pos.y);
                }
                Vec3 q = g.hero_.body.pos;
                CHECK(q.y > want - 0.3f && g.hero_.body.grounded, "gruta: %s (llega a %.1f, %.1f, %.1f; arriba %.1f)", what, q.x, q.y, q.z, top);
            };
            climb({-28.3f, -3, 88}, {-1, 0, 0}, 3, "del fondo de la Sima se sale por la escalera");
            climb({0, 3, 103.8f}, {0, 0, 1}, 11, "en el Pozo, la escalera sube a la galería");
            climb({49, -4.5f, 65.5f}, {0, 0, 1}, 2, "del agua del lago se sale por la escalera");
            // Por la grieta se sale al campo, delante de ella.
            int crack = -1;
            for (size_t i = 0; i < g.doors_.size(); ++i)
                if (g.doors_[i].arrival == arrival::kCampoGrieta) crack = int(i);
            if (crack >= 0) {
                place(g, g.doors_[size_t(crack)].pos + Vec3{0, 0.3f, 0});
                run(g, 0.2f);
                Controls f;
                f.interact = true;
                g.update(dt, f);
                for (int i = 0; i < 60 * 3 && (g.changingZone() || g.rideFade() > 0.01f); ++i) g.update(dt, Controls{});
            }
            Vec3 q = g.hero_.body.pos;
            CHECK(crack >= 0 && g.level() == LevelId::Campo && length(flat(q - g.arrivals_[size_t(arrival::kCampoGrieta)].pos)) < 0.6f,
                  "gruta: por la grieta se sale al campo (en %.1f, %.1f, %.1f)", q.x, q.y, q.z);
        }
        {  // Las Cumbres Nevadas: del sur del valle al refugio y de allí, andando por los caminos, a
            // la cruz de la cima, al lago helado y, por la pasarela, al otro lado del barranco
            Game m = fresh();
            m.loadLevel(LevelId::Cumbres);
            m.wildSpawns_ = false;  // sin Grises salvajes que tumben a la protagonista por el camino
            run(m, 0.5f);
            int inns = 0;
            for (const Npc& n : m.npcs_) inns += n.role == NpcRole::Inn;
            CHECK(m.hero_.body.grounded && inns == 1 && m.chests_.size() == 4 && m.nodes_.size() == 3 && m.props_.size() > 600,
                  "cumbres: el valle, el refugio con su guarda, 4 cápsulas, 3 nodos y %zu piezas", m.props_.size());
            auto walk = [&](std::vector<Vec3> pts) {
                for (Vec3 to : pts)
                    for (int i = 0; i < 60 * 30 && length(flat(to - m.hero_.body.pos)) > 1.2f; ++i) {
                        Controls c;
                        c.move = normalize(flat(to - m.hero_.body.pos));
                        m.update(dt, c);
                    }
                return m.hero_.body.pos;
            };
            Vec3 refuge = walk({{6, 0, -140}, {-10, 0, -95}, {2, 0, -55}, {12, 0, -22}, {10, 0, -10}});
            CHECK(length(flat(refuge - Vec3{10, 0, -10})) < 1.5f, "cumbres: por el camino al refugio (%.1f, %.1f, %.1f)", refuge.x, refuge.y, refuge.z);
            Vec3 top = walk({{14, 0, 10}, {40, 0, 34}, {22, 0, 58}, {52, 0, 84}, {34, 0, 108}, {62, 0, 132}, {70, 0, 156}, {86, 0, 174}});
            CHECK(length(flat(top - Vec3{86, 0, 174})) < 1.5f && top.y > 60, "cumbres: por las revueltas a la cima (%.1f, %.1f, %.1f)", top.x,
                  top.y, top.z);
            place(m, Vec3{10, m.groundHeight({10, 200, -10}) + 0.3f, -10});
            Vec3 lake = walk({{2, 0, -9}, {-34, 0, 12}, {-66, 0, 34}, {-86, 0, 46}, {-100, 0, 54}});
            CHECK(length(flat(lake - Vec3{-100, 0, 54})) < 1.5f, "cumbres: al lago helado (%.1f, %.1f, %.1f)", lake.x, lake.y, lake.z);
            place(m, Vec3{10, m.groundHeight({10, 200, -10}) + 0.3f, -10});
            float lowest = 1e9f;
            for (Vec3 to : {Vec3{22, 0, -4}, Vec3{56, 0, -18}, Vec3{86, 0, -26}, Vec3{114, 0, -26}, Vec3{138, 0, -35}})
                for (int i = 0; i < 60 * 30 && length(flat(to - m.hero_.body.pos)) > 1.2f; ++i) {
                    Controls c;
                    c.move = normalize(flat(to - m.hero_.body.pos));
                    m.update(dt, c);
                    if (std::fabs(m.hero_.body.pos.x - 100) < 6) lowest = std::min(lowest, m.hero_.body.pos.y);
                }
            Vec3 far = m.hero_.body.pos;
            float gorge = m.world_.terrain->heightAt(100 - 4.0f * std::sin(-26.0f / 23.0f), -26);
            CHECK(length(flat(far - Vec3{138, 0, -35})) < 1.5f && lowest > gorge + 8,
                  "cumbres: por la pasarela, sobre el barranco (lo más bajo %.1f; el fondo %.1f), a la cabaña", lowest, gorge);
        }
        {  // Las Copas: por la escalera del tronco y la pasarela larga se llega al mirador; las
            // barandillas paran al que anda, y el que cae al vacío vuelve a un sitio seguro
            Game c = fresh();
            c.loadLevel(LevelId::Copas);
            run(c, 0.5f);
            CHECK(c.hero_.body.grounded && std::fabs(c.hero_.body.pos.y) < 0.05f && c.npcs_.size() >= 7,
                  "copas: la protagonista en la plaza del tronco (y=%.2f) y %zu vecinos", c.hero_.body.pos.y, c.npcs_.size());
            {  // la escalera de caracol se sube andando, siguiendo los peldaños
                place(c, {std::sin(0.6f) * 3.7f, 0.1f, std::cos(0.6f) * 3.7f});
                float best = 0;
                for (int i = 0; i < 60 * 25; ++i) {
                    Vec3 p = c.hero_.body.pos;
                    float a = std::atan2(p.x, p.z);
                    if (a < 0) a += 2 * kPi;
                    Controls s;
                    Vec3 next{std::sin(a + 0.35f) * 3.7f, 0, std::cos(a + 0.35f) * 3.7f};
                    Vec3 to = best > 5.9f || a > 5.35f ? Vec3{0, 0, 4.0f} : next;  // arriba: al balcón
                    s.move = normalize(flat(to - p));
                    c.update(dt, s);
                    best = std::max(best, c.hero_.body.pos.y);
                }
                CHECK(c.hero_.body.pos.y > 5.9f, "copas: la escalera del tronco se sube andando (y=%.2f, lo más alto %.2f)", c.hero_.body.pos.y, best);
            }
            place(c, {0, 6.1f, 4.5f});  // en el balcón del tronco
            Controls n;
            n.move = {0, 0, 1};
            run(c, 9.0f, n);
            CHECK(c.hero_.body.pos.z > 30.5f && std::fabs(c.hero_.body.pos.y - 8.0f) < 0.2f,
                  "copas: la pasarela larga llega al mirador (z=%.1f, y=%.2f)", c.hero_.body.pos.z, c.hero_.body.pos.y);
            place(c, {6, 0.1f, 1});
            Controls e;
            e.move = {1, 0, 0};
            run(c, 4.0f, e);
            CHECK(c.hero_.body.pos.x > 9.0f && c.hero_.body.pos.x < 10.0f && c.hero_.body.pos.y > -0.1f,
                  "copas: la barandilla para al que anda (x=%.1f)", c.hero_.body.pos.x);
            place(c, {0, 0.1f, -14});  // fuera de la plaza, sobre el vacío
            run(c, 4.0f);
            CHECK(c.hero_.body.pos.y > -1.0f && c.hp_ < c.maxHp_, "copas: quien cae vuelve a un sitio seguro (y=%.1f)", c.hero_.body.pos.y);
        }
        {  // Los Andamios: cada tramo se puede hacer con lo que enseña, y no sin ello
            Game a = fresh();
            a.freePlay_ = true;  // como desde el selector: los tramos avanzan
            a.loadLevel(LevelId::Andamios);
            run(a, 0.5f);
            Character& h = a.hero_;
            CHECK(h.body.grounded && std::fabs(h.body.pos.y) < 0.05f && a.world_.ladders.size() == 2 && a.parts_.size() == 5,
                  "andamios: de pie en el rellano (y=%.2f), %zu escaleras y %zu tramos", h.body.pos.y, a.world_.ladders.size(), a.parts_.size());
            // Un salto de `from` hacia `to`: corre, salta al pasar por `takeoff` y, si hace falta,
            // el doble salto en lo alto. Devuelve la altura a la que queda de pie.
            auto hop = [&](Vec3 from, Vec3 takeoff, Vec3 to, bool twice) {
                place(a, from);
                Vec3 dir = normalize(flat(to - from));
                bool jumped = false, doubled = false;
                for (int i = 0; i < 60 * 3; ++i) {
                    Controls c;
                    c.move = length(flat(to - h.body.pos)) > 0.4f ? normalize(flat(to - h.body.pos)) : Vec3{};
                    if (!jumped && dot(flat(h.body.pos - takeoff), dir) > 0) c.jump = jumped = true;
                    else if (twice && jumped && !doubled && h.body.vel.y < 0.5f) c.jump = doubled = true;
                    a.update(dt, c);
                }
                return h.body.grounded ? h.body.pos.y : -100.0f;
            };
            {  // 1. la escalera de gato: empujando contra ella se sube y arriba se encarama
                place(a, {0, 0.1f, 3});
                Controls c;
                c.move = {0, 0, 1};
                bool climbed = false;
                for (int i = 0; i < 60 * 4; ++i) {
                    a.update(dt, c);
                    climbed = climbed || h.climb() == Character::Climb::Ladder;
                }
                CHECK(climbed && h.body.grounded && std::fabs(h.body.pos.y - 5) < 0.05f && a.part_ >= 1,
                      "andamios: por la escalera de gato a lo alto de la torre (y=%.2f, tramo %zu)", h.body.pos.y, a.part_);
                place(a, {0, 5.1f, 7.2f});  // y se baja andando hacia el borde de la escalera
                bool down = false;
                for (int i = 0; i < 60 * 4; ++i) {
                    down = down || h.climb() == Character::Climb::Ladder;
                    c.move = down && h.body.grounded ? Vec3{} : Vec3{0, 0, -1};  // abajo, se para
                    a.update(dt, c);
                }
                CHECK(down && h.body.grounded && std::fabs(h.body.pos.y) < 0.05f && !h.climbing(), "andamios: por la escalera se baja (y=%.2f)", h.body.pos.y);
                CHECK(hop({1.6f, 0.1f, 3}, {1.6f, 0, 4.5f}, {1.6f, 0, 9}, true) < 1.0f, "andamios: sin la escalera no se llega a la torre (ni con el doble salto)");
            }
            {  // 2. los saltos (el último, con doble salto; sin él no se llega)
                float y1 = hop({0, 5.1f, 7.5f}, {0, 0, 9.6f}, {0, 0, 13.5f}, false);
                float y2 = hop({0, 5.1f, 13.0f}, {0, 0, 14.6f}, {0, 0, 18.0f}, false);
                float y3 = hop({0, 6.3f, 16.8f}, {0, 0, 18.6f}, {3.0f, 0, 22.5f}, true);
                CHECK(std::fabs(y1 - 5) < 0.05f && std::fabs(y2 - 6.2f) < 0.05f && std::fabs(y3 - 8.6f) < 0.05f,
                      "andamios: de losa en losa (%.2f, %.2f, %.2f)", y1, y2, y3);
            }
            // Bot de paredes: corre hacia `first`; agarrado, salta en cuanto puede; en el aire, sigue
            // hacia donde mira y, tras un salto de pared, gasta el doble salto si `twice`. Para al
            // pisar por encima de `goal`.
            auto wallBot = [&](Vec3 from, Vec3 first, float goal, bool twice, int& clings) {
                place(a, from);
                clings = 0;
                bool started = false, wallJumped = false, doubled = false;
                float best = -100;
                for (int i = 0; i < 60 * 8; ++i) {
                    Controls c;
                    bool high = h.body.grounded && h.body.pos.y > from.y + 0.5f;  // encaramada a medio camino
                    c.move = started && !high ? h.forward() : first;
                    if (!started && i > 20) c.jump = started = true;
                    if (high && !h.climbing() && dot(h.forward(), first) > 0.95f) c.jump = true, wallJumped = true, doubled = false;  // ya vuelta
                    else if (h.climb() == Character::Climb::Wall && h.climbTime_ > 0.08f) c.jump = wallJumped = true, doubled = false;
                    else if (twice && wallJumped && !doubled && !h.climbing() && !h.body.grounded && h.body.vel.y < 1.0f) c.jump = doubled = true;
                    Character::Climb before = h.climb();
                    a.update(dt, c);
                    if (h.climb() == Character::Climb::Wall && before != Character::Climb::Wall) ++clings;
                    if (h.body.grounded && h.body.pos.y > goal - 0.1f) {
                        best = h.body.pos.y;
                        break;
                    }
                }
                return best;
            };
            {  // 3. el pozo: de pared en pared hasta arriba
                int clings = 0;
                float y = wallBot({8.2f, 8.7f, 23}, {1, 0, 0}, 15.2f, true, clings);
                CHECK(std::fabs(y - 15.2f) < 0.05f && clings >= 2, "andamios: el pozo se sube saltando de pared en pared (y=%.2f, %d agarres)", y, clings);
                CHECK(hop({4.0f, 8.7f, 21.0f}, {5.0f, 0, 21.0f}, {11.0f, 0, 21.0f}, true) < 15.0f, "andamios: el pozo no se salta con el doble salto");
            }
            {  // 4. la cornisa: agarrarse a la pared suelta, salto de pared y doble salto
                int clings = 0;
                float y = wallBot({8.6f, 16.5f, 34}, {1, 0, 0}, 21.2f, true, clings);
                CHECK(std::fabs(y - 21.2f) < 0.05f && clings >= 1, "andamios: a la cornisa con un salto de pared (y=%.2f, %d agarres)", y, clings);
                CHECK(hop({9.4f, 16.5f, 34}, {8.0f, 0, 34}, {4.0f, 0, 34}, true) < 21.0f, "andamios: a la cornisa no se llega solo con el doble salto");
            }
            {  // 5. de la cornisa a la escalera que cuelga y arriba, a la linterna
                a.part_ = 4;
                place(a, {4.5f, 21.3f, 34.5f});
                bool jumped = false, grabbed = false;
                for (int i = 0; i < 60 * 6; ++i) {
                    Controls c;
                    c.move = h.body.grounded && h.body.pos.y > 26.5f ? Vec3{} : Vec3{0, 0, 1};  // arriba, se para
                    if (!jumped && h.body.pos.z > 35.6f) c.jump = jumped = true;
                    a.update(dt, c);
                    grabbed = grabbed || h.climb() == Character::Climb::Ladder;
                }
                CHECK(grabbed && h.body.grounded && std::fabs(h.body.pos.y - 27) < 0.05f && a.part_ == 5,
                      "andamios: salto a la escalera que cuelga y arriba, a la linterna (y=%.2f, tramos %zu/5)", h.body.pos.y, a.part_);
            }
            place(a, {20, 2, 0});  // sobre el vacío: vuelve al último tramo
            run(a, 3.0f);
            CHECK(h.body.pos.y > 26 && h.body.grounded, "andamios: quien cae vuelve al último tramo (y=%.1f)", h.body.pos.y);
        }
        {  // Habitantes: pasean por su zona, sin hundirse ni salir volando
            Game h = fresh();
            h.loadLevel(LevelId::Cafetero);
            h.hero_.body.pos = {0, h.world_.groundAt(0, 30), 30};
            std::vector<Vec3> start;
            for (const Npc& n : h.npcs_) start.push_back(n.body.body.pos);
            run(h, 15.0f);
            int moved = 0, grounded = 0, home = 0;
            for (size_t i = 0; i < h.npcs_.size(); ++i) {
                const Npc& n = h.npcs_[i];
                moved += length(flat(n.body.body.pos - start[i])) > 0.5f;
                grounded += std::fabs(n.body.body.pos.y - h.groundHeight(n.body.body.pos + Vec3{0, 2, 0})) < 0.2f;
                home += length(flat(n.body.body.pos - n.home)) < n.radius + 3.0f;
            }
            int total = int(h.npcs_.size());
            CHECK(total >= 15 && moved >= total / 2 && grounded == total && home == total,
                  "habitantes del pueblo: %d, %d han paseado, %d en el suelo, %d en su zona", total, moved, grounded, home);
        }
        {  // Grises salvajes en el campo: aparecen lejos, como mucho cinco, y los que se quedan atrás se van
            Game w = fresh();
            w.loadLevel(LevelId::Campo);
            w.infHp_ = true;
            w.hero_.body.pos = {-60, w.world_.groundAt(-60, 40), 40};
            int maxAlive = 0;
            float nearest = 1e9f;
            size_t seen = 0;
            for (int s2 = 0; s2 < 60 * 45; ++s2) {
                size_t before = w.enemies_.size();
                w.update(dt, {});
                if (w.enemies_.size() > before)
                    for (size_t i = before; i < w.enemies_.size(); ++i)
                        nearest = std::min(nearest, length(flat(w.enemies_[i].body.pos - w.hero_.body.pos))), ++seen;
                int alive = 0;  // los Grises (las nubes de polillas van aparte)
                for (const Enemy& e : w.enemies_) alive += e.alive() && !isSwarm(e.kind);
                maxAlive = std::max(maxAlive, alive);
            }
            CHECK(seen >= 2 && maxAlive <= 5 && nearest > 18.0f, "campo: %zu Grises salvajes, como mucho %d a la vez, el más cercano aparece a %.0f m",
                  seen, maxAlive, nearest);
            w.lock_ = 0;
            w.hero_.body.pos = {150, w.world_.groundAt(150, -150), -150};  // se aleja: desaparecen
            w.wildTimer_ = 1000;
            w.update(dt, {});
            CHECK(w.enemies_.empty() && w.lock_ == -1, "campo: los que se quedan atrás desaparecen (%zu quedan, fijado %d)", w.enemies_.size(), w.lock_);
        }
        {  // Vecinos de la plaza: la fonda cura y guarda, la tienda abre el taller, Brasa enseña y Pabilo sigue a Vela
            Game p = fresh();
            p.loadLevel(LevelId::Plaza);
            p.enemies_.clear();
            p.encounters_.clear();
            auto find = [&](const char* name) -> Npc* {
                for (Npc& n : p.npcs_)
                    if (n.name == name) return &n;
                return nullptr;
            };
            Npc *mecha = find("Señora Mecha"), *umbra = find("Doctora Umbra"), *brasa = find("Brasa"), *pabilo = find("Pabilo");
            CHECK(mecha && umbra && brasa && pabilo && p.npcs_.size() >= 9, "plaza: %zu vecinos con la fonda, la tienda, Brasa y Pabilo",
                  p.npcs_.size());
            if (mecha && umbra && brasa && pabilo) {
                p.hp_ = 10;
                p.talkTo(*mecha);
                bool healed = p.hp_ == p.maxHp_;
                p.talkTo(*umbra);
                bool shop = p.takeShopRequest() && !p.takeShopRequest();
                p.talkTo(*brasa);
                bool tip = p.npcSpeaker_ == "Brasa" && !p.npcLine_.empty();
                CHECK(healed && shop && tip, "plaza: la fonda cura (%d), Umbra abre el taller (%d), Brasa da un consejo (%d)", healed, shop, tip);
                Vec3 home = pabilo->home;
                place(p, home + Vec3{9, 0, 0});
                Controls walk;
                walk.move = {1, 0, 0};
                run(p, 1.0f, walk);
                run(p, 4.0f);
                float gap = length(flat(pabilo->body.body.pos - p.hero_.body.pos));
                CHECK(gap < 5.0f && length(flat(pabilo->body.body.pos - home)) > 4.0f, "plaza: Pabilo sigue a Vela (a %.1f m)", gap);
            }
        }
        {  // Enjambres: las polillas apagan una farola si nadie las espanta; romper una farola suelta
            // luciérnagas, y al estallar encienden la farola apagada que tienen cerca
            auto setup = [&](Game& m) {
                m.loadLevel(LevelId::Prueba);
                m.enemies_.clear();
                m.devNoSpawns_ = true;
                m.lamps_.clear();
                Lamp l;
                l.pos = {10, 3.35f, 10};
                l.color = rgba(255, 220, 160);
                m.lamps_.push_back(l);
            };
            Game m = fresh();
            setup(m);
            place(m, {-12, 0, -12});
            for (int k = 0; k < 9; ++k) m.spawnEnemy({EnemyKind::Moth, {9.0f + 0.2f * float(k), 2.5f, 9.0f}});
            run(m, 12.0f);
            bool putOut = !m.lamps_[0].on;
            Game n = fresh();
            setup(n);
            place(n, {11.5f, 0, 10});  // al pie de la farola: las espanta
            for (int k = 0; k < 9; ++k) n.spawnEnemy({EnemyKind::Moth, {9.0f + 0.2f * float(k), 2.5f, 9.0f}});
            run(n, 12.0f);
            CHECK(putOut && n.lamps_[0].on, "polillas: apagan la farola en 10 s (%d) salvo si Vela las espanta (%d)", putOut, n.lamps_[0].on);
            Game f = fresh();
            setup(f);
            place(f, {-12, 0, -12});
            f.hitLamp(0, 100);
            f.update(dt, {});
            int flies = 0;
            for (const Enemy& e : f.enemies_) flies += e.kind == EnemyKind::Firefly;
            Lamp off = f.lamps_[0];
            off.pos = {-5, 3.35f, -5};
            off.broken = false;
            off.on = false;
            f.lamps_.push_back(off);
            f.fireflyBurst({-5, 3.0f, -5});
            CHECK(flies >= 2 && f.lamps_[1].on, "luciérnagas: salen %d de la farola rota y al estallar encienden la apagada (%d)", flies, f.lamps_[1].on);
        }
        Game g = fresh();  // contra la casa del pueblo del campo: la pared la para
        g.loadLevel(LevelId::Campo);
        const AABB* wall = nullptr;
        for (const PropInstance& p : g.props_)
            if (p.def == casa) {
                for (const AABB& a : g.propSolids_)
                    if (length(flat(a.center() - p.center)) < 1.0f) wall = &a;
                break;
            }
        CHECK(wall != nullptr, "campo: la casa tiene su caja en el mundo");
        if (wall) {
            Vec3 c = wall->center();
            float zOut = wall->max.z + 2.0f;
            place(g, {c.x, g.world_.groundAt(c.x, zOut) + 0.1f, zOut}, kPi);
            Controls c2;
            c2.move = {0, 0, -1};
            run(g, 1.5f, c2);
            CHECK(g.hero_.body.pos.z > wall->max.z - 0.01f, "campo: la pared de la casa para a la protagonista (z=%.2f, pared %.2f)",
                  g.hero_.body.pos.z, wall->max.z);
            // De día (el campo es de atardecer: se fuerza), junto al granero (sin farol) por el lado
            // contrario al sol hay sombra; en pleno prado, no.
            g.daylight_ = true;
            int barn = propLibrary().find("granero");
            const AABB* bw = nullptr;
            for (const PropInstance& p : g.props_)
                if (p.def == barn)
                    for (const AABB& a : g.propSolids_)
                        if (length(flat(a.center() - p.center)) < 1.0f) bw = &a;
            CHECK(bw != nullptr, "campo: el granero tiene su caja en el mundo");
            if (bw) {
                Vec3 shade{bw->min.x - 0.6f, 0, bw->center().z};
                shade.y = g.world_.groundAt(shade.x, shade.z);
                Vec3 open = g.heroStart_;  // el principio del camino: sin árboles ni casas
                CHECK(g.shadowAt(shade) > 0.5f && g.shadowAt(open) < 0.5f,
                      "campo: sombra del sol junto al granero (%.1f) y no en el prado (%.1f)", g.shadowAt(shade), g.shadowAt(open));
            }
        }
    }
    {  // El mapa (M): se pinta en todas las zonas, con la protagonista y las puertas dentro de la
        // imagen; en la gruta no salen los techos (se ve el suelo de las salas) y en las tabernas
        // tampoco el tejado; y fuera de un interior no hay nada
        for (int li = 0; li < int(LevelId::Count); ++li) {
            LevelId id = LevelId(li);
            if (id == LevelId::Prueba) continue;
            Game g = fresh();
            g.loadLevel(id);
            auto t0 = std::chrono::steady_clock::now();
            LevelMap m;
            g.buildLevelMap(m);
            double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
            auto inside = [&](Vec3 p) { return m.u(p) >= 0 && m.v(p) >= 0 && m.u(p) < float(m.w) && m.v(p) < float(m.h); };
            bool doors = true;
            for (const ZoneDoor& d : g.doors_) doors &= inside(d.pos);
            size_t painted = 0;
            for (uint32_t px : m.pixels) painted += (px >> 24) != 0;
            CHECK(m.w > 0 && m.h > 0 && std::max(m.w, m.h) <= 1400 && inside(g.hero_.body.pos) && doors && painted > m.pixels.size() / 20 &&
                      m.serial == g.levelSerial(),
                  "mapa de %s: %dx%d (%.2f m/px, %.0f ms), con la protagonista y las puertas dentro", g.levelName().c_str(), m.w, m.h,
                  double(m.cell), ms);
        }
        // Que mire el suelo: el color del píxel bajo la protagonista no es el de los techos de roca.
        Game g = fresh();
        g.loadLevel(LevelId::Gruta);
        LevelMap m;
        g.buildLevelMap(m);
        Vec3 room{-18, 0, 36};  // el Río Negro, al sur del río
        uint32_t px = m.pixels[size_t(int(m.v(room))) * size_t(m.w) + size_t(int(m.u(room)))];
        float lum = float(px & 0xFF) * 0.3f + float((px >> 8) & 0xFF) * 0.59f + float((px >> 16) & 0xFF) * 0.11f;
        CHECK(lum > 70, "gruta: en el mapa se ve el suelo claro de la sala, no el techo (luminancia %.0f)", double(lum));
        Game b = fresh();
        b.loadLevel(LevelId::Botequim);
        LevelMap mb;
        b.buildLevelMap(mb);
        Vec3 bar{0, 0, 0};
        uint32_t c = mb.pixels[size_t(int(mb.v(bar))) * size_t(mb.w) + size_t(int(mb.u(bar)))];
        CHECK((c >> 24) != 0 && (mb.pixels[0] >> 24) == 0, "botequim: se ve la sala por dentro y nada fuera de ella");
        // La orientación: a la derecha de quien mira a +z queda −x.
        CHECK(mb.u({-1, 0, 0}) > mb.u({1, 0, 0}) && mb.v({0, 0, 1}) < mb.v({0, 0, -1}), "mapa: arriba +z y a la derecha −x");
    }
    {  // El tema «Farolas»: las dos capas (luz y sombra) se leen y duran lo mismo (bucle sincronizado)
        StereoBuffer luz, sombra;
        std::string dir = std::string(GAME_ASSET_DIR) + "/audio/musica/";
        bool ok = loadOgg(dir + "farolas_capa_luz_loop.ogg", luz) && loadOgg(dir + "farolas_capa_sombra_loop.ogg", sombra);
        float secs = float(luz.frames()) / float(kSampleRate);
        CHECK(ok && luz.frames() == sombra.frames() && std::fabs(secs - 106.667f) < 0.05f,
              "música «Farolas»: dos capas de %.2f s, sincronizadas", double(secs));
    }
    {  // Las otras pistas grabadas y los fragmentos se leen (44,1 kHz, con algo de duración)
        const char* files[] = {"ciudad_loop", "combate_loop", "fonda_loop", "jefe_loop", "piano_nocturno_de_las_farolas",
                               "piano_vela_y_hollin", "piano_el_ultimo_farol", "stinger_victoria", "stinger_derrota"};
        std::string dir = std::string(GAME_ASSET_DIR) + "/audio/musica/";
        int read = 0;
        for (const char* f : files) {
            StereoBuffer b;
            if (loadOgg(dir + f + ".ogg", b) && b.frames() > size_t(kSampleRate) * 3) ++read;
        }
        CHECK(read == 9, "música grabada: %d de 9 pistas leídas", read);
    }
    {  // Salto y aterrizaje
        Game g = fresh();
        run(g, 0.2f);
        float maxY = 0;
        g.update(dt, press(&Controls::jump));
        for (int i = 0; i < 90; ++i) {
            g.update(dt, {});
            maxY = std::max(maxY, g.hero_.body.pos.y);
        }
        CHECK(maxY > 1.45f && maxY < 1.7f, "salto: altura max %.2f m", maxY);
        CHECK(g.hero_.body.grounded && g.hero_.body.pos.y == 0, "salto: aterriza (y=%.2f)", g.hero_.body.pos.y);
    }
    {  // Escalones de la terraza (0,3 m) se suben andando; el pilón de la fuente (0,7 m) no
        Game g = fresh();
        place(g, {10.5f, 0, 5.0f});
        Controls c;
        c.move = {0, 0, 1};
        run(g, 1.2f, c);
        CHECK(std::fabs(g.hero_.body.pos.y - 0.9f) < 1e-3f, "terraza por los escalones: y=%.2f", g.hero_.body.pos.y);
        Game h = fresh();
        place(h, {0, 0, -4.0f});
        run(h, 1.0f, c);
        CHECK(h.hero_.body.pos.y < 0.01f && h.hero_.body.pos.z < -2.6f, "pilon de la fuente: bloquea sin saltar (z=%.2f)",
              h.hero_.body.pos.z);
    }
    {  // Combo de suelo: 3 golpes + remate, con daño según el arma
        Game g = fresh();
        place(g, {0, 0, -10});
        Enemy& e = addEnemy(g, EnemyKind::Brute, {0, 0, -8.6f});
        (void)e;
        std::vector<Move> seen;
        for (int i = 0; i < 4; ++i) {
            g.update(dt, press(&Controls::attack));
            for (int k = 0; k < 40; ++k) {
                if (seen.empty() || seen.back() != g.hero_.move())
                    if (g.hero_.move() != Move::None) seen.push_back(g.hero_.move());
                g.update(dt, {});
            }
        }
        bool order = seen.size() >= 4 && seen[0] == Move::Slash1 && seen[1] == Move::Slash2 &&
                     seen[2] == Move::Slash3 && seen[3] == Move::Finisher;
        CHECK(order, "combo: tajo, reves, giro y remate (%zu movimientos)", seen.size());
        float dealt = kBruteHp - g.enemies_[0].health;
        CHECK(std::fabs(dealt - 54) < 1e-3f, "dano del combo: %.0f (10+10+12+22 con la espada de madera)", dealt);
    }
    {  // Un combo completo derrota a un duende, que suelta dinero y orbes
        Game g = fresh();
        place(g, {0, 0, -10});
        addEnemy(g, EnemyKind::Dazzled, {0, 0, -8.8f});
        for (int i = 0; i < 4; ++i) run(g, 0.6f, press(&Controls::attack));
        CHECK(g.kills_ == 1, "duende derrotado con el combo (derrotados %d)", g.kills_);
        int coins = 0;
        for (const Pickup& p : g.pickups_) coins += p.kind == PickupKind::Munny ? p.value : 0;
        CHECK(g.inventory().munny + coins >= 3, "suelta dinero: %d en el suelo + %d recogido", coins, g.inventory().munny);
        run(g, 3.0f);
        CHECK(g.enemies_.empty(), "se desvanece y desaparece");
        CHECK(g.pickups_.empty() && g.inventory().munny >= 3, "el botin vuela hacia el jugador y se recoge (dinero %d)",
              g.inventory().munny);
    }
    {  // Acercamiento automático: el golpe arrastra al jugador hacia el objetivo
        Game g = fresh();
        place(g, {0, 0, -12});
        addEnemy(g, EnemyKind::Brute, {0, 0, -8.0f});
        run(g, 0.3f, press(&Controls::attack));
        float moved = g.hero_.body.pos.z + 12;
        CHECK(moved > 1.5f, "golpe a 4 m: se acerca %.2f m", moved);
        CHECK(g.enemies_[0].health < kBruteHp, "y alcanza a la seta (vida %.0f)", g.enemies_[0].health);
    }
    {  // Combo aéreo: flota mientras golpea y solo hay un remate por salto
        Game g = fresh();
        place(g, {0, 0, -10});
        run(g, 0.1f);
        g.update(dt, press(&Controls::jump));
        run(g, 0.25f);
        float airTime = 0.27f;
        std::vector<Move> seen;
        Controls atk = press(&Controls::attack);
        for (int i = 0; i < 300 && !g.hero_.body.grounded; ++i) {
            g.update(dt, i % 12 == 0 ? atk : Controls{});
            airTime += dt;
            if (g.hero_.move() != Move::None && (seen.empty() || seen.back() != g.hero_.move())) seen.push_back(g.hero_.move());
        }
        bool order = seen.size() == 3 && seen[0] == Move::Air1 && seen[1] == Move::Air2 && seen[2] == Move::AirFinisher;
        CHECK(order, "combo aereo: 2 golpes + remate (%zu movimientos)", seen.size());
        CHECK(airTime > 1.2f, "flota durante el combo: %.2f s en el aire (un salto normal ~0,8 s)", airTime);
    }
    {  // IA: el duende persigue y golpea; la voltereta esquiva; la guardia bloquea
        Game g = fresh();
        place(g, {0, 0, -10});
        addEnemy(g, EnemyKind::Dazzled, {0, 0, -5}, AiState::Chase);
        run(g, 3.0f);
        CHECK(g.hp_ < 100, "el duende persigue y ataca: PV %.0f", g.hp_);

        Game r = fresh();
        place(r, {0, 0, -10});
        Enemy& e = addEnemy(r, EnemyKind::Dazzled, {0, 0, -8.2f}, AiState::Chase);
        e.yaw = kPi;
        r.enemyStartAttack(e);
        Controls d = press(&Controls::dodge);
        d.move = {1, 0, 0};
        run(r, 0.5f, d);
        CHECK(r.hp_ == 100, "la voltereta lo esquiva: PV %.0f", r.hp_);

        Game b = fresh();
        place(b, {0, 0, -10});
        Enemy& eb = addEnemy(b, EnemyKind::Dazzled, {0, 0, -8.6f}, AiState::Chase);
        Controls guard;
        guard.guard = true;
        run(b, 0.1f, guard);  // guardia recién levantada: bloqueo perfecto
        b.enemyStartAttack(eb);
        run(b, 0.3f, guard);
        CHECK(b.hp_ == 100, "guardia de frente: sin dano (PV %.0f)", b.hp_);
        CHECK(b.enemies_[0].state == AiState::Stagger, "bloqueo perfecto: el duende se tambalea (%s)",
              aiStateName(b.enemies_[0].state));
        CHECK(b.enemies_[0].stun > 1.5f && b.riposteWindow() > 0, "bloqueo perfecto: queda aturdido (%.1f s) y se abre el contraataque",
              b.enemies_[0].stun);
        run(b, 1.0f);
        CHECK(b.enemies_[0].state == AiState::Stagger, "sigue aturdido un segundo después");
        // Súper contraataque: atacar en la ventana encadena solo la ráfaga, con el doble de daño.
        b.enemies_[0].health = 1000;
        float h0 = b.enemies_[0].health;
        run(b, 0.05f, press(&Controls::attack));
        CHECK(b.riposting(), "atacar con el enemigo aturdido: empieza el súper contraataque");
        int moves = 0;
        Move last = Move::None;
        for (int k = 0; k < 600 && b.riposting(); ++k) {
            b.update(dt, {});
            if (b.hero_.move() != last && b.hero_.move() != Move::None) ++moves;
            last = b.hero_.move();
        }
        float dealt = h0 - b.enemies_[0].health;
        CHECK(!b.riposting() && moves == 5 && b.hp_ == 100, "la ráfaga encadena %d golpes sola y termina sin recibir daño", moves);
        CHECK(dealt > 60, "con el doble de daño (%.0f)", dealt);
        // En sombra, con el mazo: 4 golpes, el último el salto que agrieta el suelo.
        b.unlockShadow();
        b.freePlay_ = true;
        b.update(dt, press(&Controls::swapMode));
        b.freePlay_ = false;
        run(b, 1.0f);
        b.enemies_[0].stun = 2.0f;
        b.enemies_[0].state = AiState::Stagger;
        b.enemies_[0].stateTime = 0;
        b.riposteWindow_ = 1.0f;
        b.riposteTarget_ = 0;
        run(b, 0.05f, press(&Controls::attack));
        moves = 0;
        bool quake = false;
        for (int k = 0; k < 600 && b.riposting(); ++k) {
            b.update(dt, {});
            if (b.hero_.move() != last && b.hero_.move() != Move::None) ++moves;
            quake |= b.hero_.move() == Move::Quake;
            last = b.hero_.move();
        }
        CHECK(b.shadowMode() && moves == 4 && quake, "en sombra, con el mazo: %d golpes y remate de salto", moves);
    }
    {  // Seta: la onda al caer se esquiva saltando
        Game g = fresh();
        place(g, {0, 0, -10});
        Enemy& e = addEnemy(g, EnemyKind::Brute, {0, 0, -8.0f}, AiState::Chase);
        g.enemyStartAttack(e);
        run(g, 1.5f);
        float hit = g.hp_;
        Game j = fresh();
        place(j, {0, 0, -10});
        Enemy& ej = addEnemy(j, EnemyKind::Brute, {0, 0, -8.0f}, AiState::Chase);
        j.enemyStartAttack(ej);
        run(j, 0.5f);
        j.update(dt, press(&Controls::jump));
        run(j, 1.0f);
        CHECK(hit < 100 && j.hp_ == 100, "onda de la seta: quieto %.0f PV, saltando %.0f PV", hit, j.hp_);
    }
    {  // Técnicas de luz: la lanza gasta Luz y daña; Calidez cura; sin Luz no salen
        Game g = fresh();
        place(g, {0, 0, -10});
        addEnemy(g, EnemyKind::Brute, {0, 0, -5});
        float l0 = g.mp_;
        Controls c;
        c.command = 0;
        run(g, 1.5f, c);
        CHECK(std::fabs(g.mp_ - (l0 - 20)) < 1e-3f, "la lanza de luz gasta 20 de Luz (queda %.0f)", g.mp_);
        CHECK(g.enemies_[0].health < kBruteHp, "y daña a la seta (vida %.0f)", g.enemies_[0].health);
        g.hp_ = 40;
        g.mp_ = 60;
        c.command = 2;
        run(g, 1.0f, c);
        CHECK(g.hp_ > 80, "Calidez: de 40 a %.0f PV", g.hp_);
        g.mp_ = 5;
        size_t before = g.projectiles_.size();
        c.command = 1;
        run(g, 0.6f, c);
        CHECK(g.projectiles_.size() == before && g.mp_ >= 5, "sin Luz no sale la Refraccion");
    }
    {  // El Poder: no se recarga solo; Q lejos de una fuente no hace nada; Supernova
        Game g = fresh();
        g.nodes_[0].lit = true;
        g.nodes_[0].litTime = 10;
        place(g, g.nodes_[0].pos + Vec3{1.5f, 0, 0});
        g.mp_ = 10;
        run(g, 2.0f);
        CHECK(g.mp_ == 10, "en la luz el Poder no se recarga solo (solo pociones y orbes)");
        // Lejos de las farolas, Q no hace nada (ya no hay haz que gaste Luz).
        place(g, {0, 0, -12});
        g.mp_ = 50;
        Vec3 p0 = g.hero_.body.pos;
        run(g, 0.8f, press(&Controls::light));
        CHECK(g.mp_ >= 50 && !g.insideLight() && !g.travelingByLight() && length(flat(g.hero_.body.pos - p0)) < 0.1f,
              "Q lejos de una fuente de luz: no se mueve ni gasta Luz");
        // Supernova: solo con el Poder lleno.
        g.enemies_.clear();
        for (int i = 0; i < 4; ++i) addEnemy(g, EnemyKind::Brute, g.hero_.body.pos + Vec3{std::cos(float(i)) * 3, 0, std::sin(float(i)) * 3});
        g.mp_ = 50;
        run(g, 0.5f, command(4));
        CHECK(g.enemies_[0].health == kBruteHp, "sin el Poder lleno no hay Supernova");
        g.mp_ = 100;
        float c0 = g.croma_;
        run(g, 1.0f, command(4));
        bool all = true;
        for (const Enemy& en : g.enemies_) all = all && en.health < kBruteHp - 25;
        CHECK(all && g.mp_ == 0 && g.croma_ > c0, "Supernova (técnica 5): daña a todos alrededor, gasta el Poder y devuelve croma");
    }
    {  // Hielo congela: el enemigo va más lento
        Game g = fresh();
        place(g, {0, 0, -10});
        addEnemy(g, EnemyKind::Brute, {0, 0, -6});
        Controls c;
        c.command = 1;
        run(g, 0.8f, c);
        CHECK(g.enemies_[0].freeze > 0, "Hielo congela a la seta (%.1f s)", g.enemies_[0].freeze);
    }
    {  // Objetos: pociones y equipar armas
        Game g = fresh();
        int potions = g.inventory().count(ItemId::Potion);
        CHECK(!g.useItem(ItemId::Potion), "con la vida llena la pocion no se gasta");
        g.hp_ = 30;
        CHECK(g.useItem(ItemId::Potion) && g.hp_ == 70 && g.inventory().count(ItemId::Potion) == potions - 1,
              "pocion: +40 PV (PV %.0f, quedan %d)", g.hp_, g.inventory().count(ItemId::Potion));
        g.hp_ = 50;
        Controls c;
        c.command = kSpellSlots;  // primer atajo de objetos: pocion
        run(g, 0.1f, c);
        CHECK(g.hp_ == 90, "atajo del menu de comandos usa la pocion (PV %.0f)", g.hp_);
        CHECK(!g.equip(ItemId::StarStaff), "no se puede equipar un arma que no se tiene");
        g.inventory().add(ItemId::StarStaff);
        CHECK(g.equip(ItemId::StarStaff) && g.hero_.comboHits == 4, "baculo equipado: combo de %d golpes + remate",
              g.hero_.comboHits);
        CHECK(g.inventory().add(ItemId::Potion, 500) < 500 && g.inventory().count(ItemId::Potion) == 99,
              "maximo 99 por objeto");
    }
    {  // Taller: fabricar con piezas, armas nuevas y afiladores
        Game g = fresh();
        int rc = 0;
        const Recipe* rs = recipes(rc);
        const Recipe* lance = nullptr;
        const Recipe* sharp = nullptr;
        for (int i = 0; i < rc; ++i) {
            if (rs[i].result == ItemId::LightLance) lance = &rs[i];
            if (rs[i].result == ItemId::Sharpener) sharp = &rs[i];
        }
        CHECK(lance && !g.craft(*lance), "sin piezas no se fabrica");
        g.inventory().add(ItemId::ShinyShard, 7);
        g.inventory().add(ItemId::GoldSpore, 2);
        g.inventory().add(ItemId::WingDust, 3);
        CHECK(g.craft(*lance) && g.inventory().count(ItemId::LightLance) == 1 && g.inventory().count(ItemId::ShinyShard) == 4,
              "lanza de luz fabricada (gasta 3 estaticas y 2 esquirlas)");
        CHECK(!g.craft(*lance), "un arma solo se fabrica una vez");
        CHECK(g.equip(ItemId::LightLance) && g.hero_.moveSet == MoveSet::Lance, "con la lanza, combo de estocadas");
        for (int i = 0; i < 2; ++i) g.craft(*sharp);
        g.useItem(ItemId::Sharpener);
        g.useItem(ItemId::Sharpener);
        CHECK(g.inventory().powerBonus == 4, "dos afiladores: fuerza +4");
        {  // Espadas de la forja: piden nivel además de piezas
            Game f = fresh();
            const Recipe* dusk = nullptr;
            for (int i = 0; i < rc; ++i)
                if (rs[i].result == ItemId::DuskBlade) dusk = &rs[i];
            f.inventory().add(ItemId::ShinyShard, 10);
            f.inventory().add(ItemId::WingDust, 10);
            CHECK(dusk && !f.craft(*dusk), "la hoja crepuscular no se forja a nivel 1 aunque haya piezas");
            f.gainXp(10000);
            CHECK(f.craft(*dusk) && f.equip(ItemId::DuskBlade) && f.hero_.comboHits == 4,
                  "a nivel %d sí: se equipa y su combo es más largo", f.heroLevel());
            int swords = 0;
            for (int i = 0; i < rc; ++i) swords += itemDef(rs[i].result).weapon.style == WeaponStyle::Sword && rs[i].minLevel > 1;
            CHECK(swords >= 5, "hay %d espadas para forjar según se avanza", swords);
        }
        g.inventory().add(ItemId::Sharpener, 3);
        for (int i = 0; i < 3; ++i) g.useItem(ItemId::Sharpener);
        CHECK(g.inventory().powerBonus == 6 && g.inventory().count(ItemId::Sharpener) == 2, "el filo se queda en +6");
    }
    {  // Aprendizaje por refuerzo de los Grises
        Brain b;
        b.epsilon = 0;
        int st = Brain::state({1.5f, 0, 1.0f, true, true});
        for (int i = 0; i < 30; ++i) b.learn(st, Intent::Attack, 1.0f, st, true);
        b.learn(st, Intent::Wait, -0.5f, st, true);
        CHECK(b.q(st, Intent::Attack) > 0.9f && b.choose(st, 0.5f, 0.5f) == Intent::Attack,
              "Q-learning: una tactica premiada pasa a ser la elegida (Q=%.2f)", b.q(st, Intent::Attack));
        int guarding = Brain::state({1.5f, 2, 1.0f, true, true});
        CHECK(guarding != st, "ver al Prismero en guardia es otra situacion distinta");
        Brain c;
        std::string path = "/tmp/unamed_game_test.q";
        CHECK(b.save(path) && c.load(path) && c.q(st, Intent::Attack) == b.q(st, Intent::Attack) && c.experience() == 31,
              "el cerebro se guarda y se carga igual");
        std::remove(path.c_str());

        Game g = fresh();
        place(g, {0, 0, -10});
        addEnemy(g, EnemyKind::Dazzled, {0, 0, -7}, AiState::Chase);
        run(g, 12.0f);
        const Brain& imp = g.brain(int(EnemyKind::Dazzled));
        float bestAttack = -1e9f;
        for (int s2 = 0; s2 < Brain::kStates; ++s2) bestAttack = std::max(bestAttack, imp.q(s2, Intent::Attack));
        CHECK(g.hp_ < 100 && imp.experience() > 5 && bestAttack > 0,
              "el Rastrero aprende jugando: %d decisiones, atacar vale %.2f cuando acierta", imp.experience(), bestAttack);
        CHECK(g.bossBrain().experience() == 0, "el jefe tiene su propio cerebro (aun sin estrenar)");
    }
    {  // Viaje por la luz: entrar en una farola, saltar a otra, salir y emboscada
        Game g = fresh();
        Vec3 lampA = g.lamps_[1].pos, lampB = g.lamps_[2].pos;  // (5,5, 3,35, -5,5) y (-5,5, 3,35, -5,5)
        place(g, {0, 0, -12});
        g.update(dt, {});
        CHECK(g.lightTarget() < 0, "lejos de las farolas no hay fuente en la que entrar");
        place(g, {lampA.x, 0, lampA.z - 2.0f});
        g.update(dt, {});
        CHECK(g.lightTarget() >= 0 && length(g.lightTargetPos() - lampA) < 0.01f, "junto a una farola, queda señalada");
        float l0 = g.mp_;
        run(g, 1.0f, press(&Controls::light));
        CHECK(g.insideLight() && length(g.hostLight() - lampA) < 0.01f && g.mp_ < l0, "Q: entra en la farola (gasta Poder)");
        // Dentro no le ven ni le hieren.
        Enemy& imp = addEnemy(g, EnemyKind::Dazzled, {lampA.x + 3, 0, lampA.z}, AiState::Chase);
        g.hurtHero({1, 0, 0}, 30, 2, &imp);
        run(g, 0.3f);
        CHECK(g.hp_ == 100 && g.enemies_[0].state == AiState::Idle, "dentro de la luz los Grises no le ven ni le hieren");
        g.enemies_.clear();
        // Saltar a otra farola apuntando con la cámara.
        Controls aim;
        aim.aimOrigin = lampA + Vec3{3, 0.5f, -2};
        aim.aimDir = normalize(lampB - aim.aimOrigin);
        g.update(dt, aim);
        CHECK(g.lightTarget() >= 0 && length(g.lightTargetPos() - lampB) < 0.01f, "dentro: la cámara señala otra farola");
        Controls go = aim;
        go.light = true;
        run(g, 1.0f, go);
        CHECK(g.insideLight() && length(g.hostLight() - lampB) < 0.01f, "Q: salta de farola en farola");
        // R elige fuente sin apuntar; la cámara la sigue.
        Controls r = press(&Controls::lockOn);
        r.camForward = {0, 0, 1};
        run(g, 0.1f, r);
        Vec3 lockPos;
        CHECK(g.lightLock_ && g.lockTarget(lockPos) && g.lightTarget() >= 0, "R dentro de la luz: elige una fuente y la cámara la mira");
        // Lo que tapa la luz impide saltar.
        g.lightLock_ = false;
        g.blocks_.push_back({boxFromCenter((lampA + lampB) * 0.5f, {0.3f, 2.0f, 2.0f}), 0, Material::Stone});
        g.rebuildSolids();
        Controls back;
        back.aimOrigin = lampB + Vec3{-3, 0.5f, -2};
        back.aimDir = normalize(lampA - back.aimOrigin);
        g.update(dt, back);
        CHECK(g.lightTarget() < 0 || length(g.lightTargetPos() - lampA) > 0.1f, "solo se salta a luces a la vista");
        g.blocks_.pop_back();
        g.rebuildSolids();
        // Salir cuando se quiere: junto a la farola, en el suelo.
        run(g, 1.0f, press(&Controls::jump));
        float d = length(flat(g.hero_.body.pos - lampB));
        CHECK(!g.insideLight() && !g.travelingByLight() && d < 1.5f && g.hero_.body.grounded, "Espacio: sale junto a la farola (a %.2f m)", d);
        // Emboscada: desde dentro, el clic cae sobre el Gris más cercano.
        run(g, 1.0f, press(&Controls::light));
        Enemy& shroom = addEnemy(g, EnemyKind::Brute, {lampB.x - 4, 0, lampB.z + 2});
        float hp0 = shroom.health;
        run(g, 1.0f, press(&Controls::attack));
        CHECK(!g.insideLight() && g.enemies_[0].health < hp0 && length(flat(g.hero_.body.pos - g.enemies_[0].body.pos)) < 2.5f,
              "clic dentro de la luz: emboscada sobre el Gris (vida %.0f → %.0f)", hp0, g.enemies_[0].health);
        // Hasta lo alto de una torre del puente: se entra desde su base.
        Game p = fresh();
        p.loadLevel(LevelId::Puente);
        place(p, {4.4f, 0.8f, 29.5f});
        run(p, 1.0f, press(&Controls::light));
        CHECK(p.insideLight() && p.hostLight().y > 8.0f, "desde la base de una torre se entra en la luz de lo alto");
        run(p, 1.0f, press(&Controls::jump));
        CHECK(p.hero_.body.pos.y > 8.0f && p.hero_.body.grounded, "al salir queda en lo alto de la torre (y=%.1f)", p.hero_.body.pos.y);
        // Una farola apagada no sirve.
        Game q = fresh();
        q.lamps_[1].on = false;
        place(q, {lampA.x, 0, lampA.z - 2.0f});
        q.update(dt, {});
        CHECK(q.lightTarget() < 0, "en una farola apagada no se puede entrar");
        // En la luz, los Grises se debilitan y la Luz se recarga sola.
        Game w = fresh();
        w.nodes_[0].lit = true;
        w.nodes_[0].litTime = 10;
        addEnemy(w, EnemyKind::Brute, w.nodes_[0].pos + Vec3{1, 0, 0});
        addEnemy(w, EnemyKind::Brute, {0, 0, 10});
        w.hitEnemy(w.enemies_[0], {1, 0, 0}, 10, 0, 0, false);
        w.hitEnemy(w.enemies_[1], {1, 0, 0}, 10, 0, 0, false);
        CHECK(w.enemies_[0].health == kBruteHp - 15 && w.enemies_[1].health == kBruteHp - 10, "en la luz el golpe a uno de sombra hace un 50%% mas");
        addEnemy(w, EnemyKind::Dazzled, w.nodes_[0].pos + Vec3{-1, 0, 0});
        w.hitEnemy(w.enemies_[2], {1, 0, 0}, 10, 0, 0, false);
        CHECK(w.enemies_[2].health == enemyDef(EnemyKind::Dazzled).maxHealth - 8, "a uno de luz, en la luz, un 20%% menos");

    }
    {  // Corte cargado: mantener el clic carga; al soltar, la onda atraviesa a varios
        Game g = fresh();
        place(g, {0, 0, -12});
        addEnemy(g, EnemyKind::Brute, {0, 0, -7});
        addEnemy(g, EnemyKind::Brute, {0, 0, -3});
        for (Enemy& e : g.enemies_) {
            e.state = AiState::Recover;
            e.stateTime = -100;
        }
        Controls hold;
        hold.attackHeld = true;
        hold.camForward = {0, 0, 1};
        for (int i = 0; i < 80; ++i) g.update(dt, hold);
        CHECK(g.hero_.charge() >= 1.0f, "carga completa al mantener el clic");
        run(g, 1.5f);
        CHECK(g.enemies_[0].health < kBruteHp && g.enemies_[1].health < kBruteHp, "la onda alcanza a los dos Domos (%.0f, %.0f)",
              g.enemies_[0].health, g.enemies_[1].health);
    }
    {  // Transformaciones y compañeros
        Game g = fresh();
        place(g, {0, 0, -10});
        float speed0 = g.hero_.attackSpeed;
        g.mp_ = 30;
        g.update(dt, press(&Controls::transform));
        CHECK(g.form() == Form::None, "sin 50 de Poder no hay transformacion");
        g.mp_ = 100;
        g.update(dt, press(&Controls::transform));
        CHECK(g.form() == Form::Solar && g.hero_.attackSpeed > speed0 * 1.3f && g.mp_ == 50,
              "en la luz, Forma Solar: gasta 50 de Poder y golpea mas rapido");
        run(g, 21.0f);
        CHECK(g.form() == Form::None && std::fabs(g.hero_.attackSpeed - speed0) < 1e-4f, "la forma dura 20 s");
        g.unlockShadow();
        g.shadowMode_ = true;
        g.inv_.shadowSide = true;
        g.applyWeapon();
        g.mp_ = 100;
        g.update(dt, press(&Controls::transform));
        CHECK(g.form() == Form::Wolf && g.hero_.moveSet == MoveSet::Fists && g.hero_.speedScale > 1.4f,
              "en la sombra, Lobo de sombra: muerde rapido y corre mucho");
        Vec3 w0 = g.wolf_.body.pos;
        Controls fwd;
        fwd.move = {1, 0, 0};
        run(g, 0.5f, fwd);
        CHECK(length(g.wolf_.body.pos - g.hero_.body.pos) < 0.01f && length(g.wolf_.body.pos - w0) > 1.0f, "el lobo va con el protagonista");
        g.shadowMode_ = false;  // (directo, sin pasar por swapMode)
        g.unlockShadow();
        g.swapCooldown_ = 0;
        g.shadowMode_ = true;
        g.swapMode();
        CHECK(g.form() == Form::None, "cambiar de mitad deshace la forma");

        Game h = fresh();
        place(h, {0, 0, -10});
        addEnemy(h, EnemyKind::Brute, {0, 0, -5});
        h.mp_ = 30;
        h.update(dt, press(&Controls::summon));
        CHECK(h.companions_.empty(), "la chispa necesita 40 de Luz");
        h.mp_ = 50;
        h.update(dt, press(&Controls::summon));
        CHECK(h.companions_.size() == 1 && h.mp_ == 10, "chispa aliada invocada (cuesta 40)");
        run(h, 4.0f);
        CHECK(h.enemies_[0].health < kBruteHp, "la chispa dispara al Domo (vida %.0f)", h.enemies_[0].health);
        h.inventory().add(ItemId::NoraLantern);
        h.mp_ = 60;
        h.hp_ = 50;
        h.update(dt, press(&Controls::summon));
        CHECK(h.companions_.size() == 2 && h.companions_[1].kind == 1, "con el farolillo acude Nora");
        run(h, 3.5f);
        CHECK(h.hp_ > 50, "la luz de Nora cura (PV %.0f)", h.hp_);
    }
    {  // Cofres y cajas
        Game g = fresh();
        place(g, {11.5f, 0.9f, 11.4f});
        run(g, 0.2f);
        run(g, 0.2f, press(&Controls::interact));
        CHECK(g.chests_[1].open && g.inventory().count(ItemId::StarStaff) == 1, "cofre de la terraza: prisma estelar");
        CHECK(!g.notices_.empty(), "aviso de objeto obtenido (%s)", g.notices_.empty() ? "-" : g.notices_[0].text.c_str());

        Game k = fresh();
        size_t solids = k.world_.solids.size();
        place(k, {-12.0f, 0, -13.4f});
        for (int i = 0; i < 2; ++i) run(k, 0.6f, press(&Controls::attack));
        size_t broken = 0;
        for (const Crate& cr : k.crates_) broken += cr.broken;
        CHECK(k.crates_[0].broken && k.world_.solids.size() == solids - broken,
              "caja rota con dos golpes (%zu rotas, ya no son solidas)", broken);
        CHECK(!k.pickups_.empty() || k.inventory().munny > 0, "y suelta botin");
    }
    {  // Fijar objetivo: elige el que tiene delante la cámara y pasa al siguiente
        Game g = fresh();
        place(g, {0, 0, -10});
        addEnemy(g, EnemyKind::Dazzled, {6, 0, -10});
        addEnemy(g, EnemyKind::Dazzled, {0, 0, -4});
        Controls c = press(&Controls::lockOn);
        c.camForward = {0, 0, 1};
        g.update(dt, c);
        CHECK(g.lock_ == 1, "fija al enemigo de delante de la camara (%d)", g.lock_);
        Controls next = press(&Controls::nextTarget);
        next.camForward = {0, 0, 1};
        g.update(dt, next);
        CHECK(g.lock_ == 0, "clic central: pasa al siguiente (%d)", g.lock_);
        g.update(dt, c);
        CHECK(g.lock_ == -1, "R otra vez: suelta el objetivo");
        g.update(dt, c);
        g.update(dt, press(&Controls::unlock));
        CHECK(g.lock_ == -1, "X también lo suelta");
    }
    {  // Polilla: su orbe se devuelve de un golpe y la daña
        Game g = fresh();
        place(g, {0, 0, -10});
        Enemy& m = addEnemy(g, EnemyKind::Watcher, {0, 0, -4}, AiState::Chase);
        g.enemyStartAttack(m);
        m.state = AiState::Recover;
        m.stateTime = -10;  // que no vuelva a disparar durante el test
        bool reflected = false;
        for (int i = 0; i < 120 && !reflected; ++i) {
            bool close = !g.projectiles_.empty() && length(g.projectiles_[0].pos - g.hero_.body.pos) < 3.2f;
            g.update(dt, close ? press(&Controls::attack) : Controls{});
            reflected = !g.projectiles_.empty() && g.projectiles_[0].reflected;
        }
        run(g, 1.5f);
        CHECK(reflected, "orbe devuelto con la espada");
        CHECK(g.enemies_.empty() || g.enemies_[0].health < 24, "y golpea a la polilla");
        CHECK(g.hp_ == 100, "sin dano al jugador (PV %.0f)", g.hp_);
    }
    {  // El color es energía: cada Gris disipado y cada nodo encendido devuelven croma
        Game g = fresh();
        float c0 = g.croma_;
        CHECK(std::fabs(c0 - 0.05f) < 1e-4f, "cada escenario empieza casi sin color (croma %.0f%%)", c0 * 100);
        for (int i = 0; i < 3; ++i) {
            g.enemies_.push_back(makeEnemy(EnemyKind::Dazzled, {0, 0, 5}, 0, 0));
            g.killEnemy(g.enemies_.back());
        }
        CHECK(std::fabs(g.croma_ - (c0 + 3 * 0.015f)) < 1e-4f && g.splashes_.size() == 3,
              "3 Grises disipados: croma %.1f%%, %zu salpicaduras", g.croma_ * 100, g.splashes_.size());
        run(g, 1.0f);
        g.enemies_.clear();
        g.enemies_.push_back(makeEnemy(EnemyKind::Dazzled, g.nodes_[0].pos + Vec3{3, 0, 0}, 0, 0));
        g.enemies_.back().state = AiState::Recover;
        g.enemies_.back().stateTime = -100;
        place(g, g.nodes_[0].pos + Vec3{1.0f, 0, 0});
        g.update(dt, press(&Controls::interact));
        CHECK(!g.nodes_[0].lit, "con un Gris cerca el nodo no se enciende");
        g.enemies_.clear();
        float c1 = g.croma_;
        g.update(dt, press(&Controls::interact));
        CHECK(g.nodes_[0].lit && std::fabs(g.croma_ - (c1 + 0.18f)) < 1e-4f, "nodo encendido: +18%% de croma");
        for (int i = 0; i < 80; ++i) {
            g.enemies_.push_back(makeEnemy(EnemyKind::Dazzled, {0, 0, 5}, 0, 0));
            g.killEnemy(g.enemies_.back());
        }
        CHECK(g.croma_ == 1.0f && g.splashes_.size() == size_t(Renderer::kMaxPaints),
              "el croma se queda en el 100%% y se guardan las %zu salpicaduras mas recientes", g.splashes_.size());
    }
    {  // Escenarios por partes: de la plaza al mercado
        Game g;
        g.init(GAME_ASSET_DIR);
        skipDialogues(g);
        run(g, 0.5f);
        run(g, 3.0f);
        CHECK(g.level() == LevelId::Plaza && g.partIndex() == 0 && g.hero_.lying == 0 && g.powers().attack &&
                  g.powers().strong && g.enemies_.size() == 2 && g.enemies_[0].kind == EnemyKind::Hound,
              "plaza: se empieza de pie, con la espada, y llegan dos sabuesos (%zu)", g.enemies_.size());
        for (Enemy& e : g.enemies_) g.killEnemy(e);
        run(g, 1.0f);
        skipDialogues(g);
        CHECK(g.partIndex() == 1, "combate ganado: parte 2, los nodos");
        CHECK(g.gates_[0].closed, "la puerta del mercado sigue cerrada");
        for (Encounter& e : g.encounters_) e.cleared = true;  // sin los guardianes de los nodos
        for (size_t i = 0; i < g.nodes_.size(); ++i) {
            place(g, g.nodes_[i].pos + Vec3{0.9f, 0.1f, 0});
            g.update(dt, press(&Controls::interact));
            skipDialogues(g);
        }
        run(g, 0.2f);
        skipDialogues(g);
        CHECK(g.partIndex() == 2 && g.gates_[0].closed && !g.receptors_[0].lit && g.powers().light,
              "tres nodos: vuelve la luz; la puerta sigue sellada (rompecabezas)");
        // Rompecabezas: girar los espejos (F) hasta que el haz llegue al receptor.
        place(g, g.mirrors_[0].pos + Vec3{0.9f, 0, 0});
        int before = g.mirrors_[0].angle;
        g.update(dt, press(&Controls::interact));
        CHECK(g.mirrors_[0].angle == (before + 1) % 4, "F gira el espejo 45 grados");
        g.mirrors_[0].angle = 3;
        g.mirrors_[1].angle = 1;
        g.mirrors_[2].angle = 1;
        run(g, 0.3f);
        skipDialogues(g);
        run(g, 0.2f);
        CHECK(g.receptors_[0].lit && !g.gates_[0].closed && g.partIndex() == 3,
              "el haz llega a la puerta: se abre (recorrido de %zu tramos)", g.beamColors_[0].size());
        place(g, {0, 0, 24});
        run(g, 2.0f);
        CHECK(g.level() == LevelId::Mercado && g.levelName() == "Mercado Quemado" && std::fabs(g.croma_ - 0.05f) < 1e-3f,
              "al cruzar la puerta se carga el mercado, otra vez casi sin color");
        // Cámara opcional: solo se abre con luz magenta (el segundo espejo la tiñe).
        skipDialogues(g);
        g.mirrors_[0].angle = 1;
        g.mirrors_[1].angle = 1;
        g.mirrors_[1].filter = 0;
        run(g, 0.1f);
        bool vault = false;
        for (const Gate& gt : g.gates_) if (gt.id == "camara") vault = !gt.closed;
        CHECK(!g.receptors_[0].lit && !vault, "con luz blanca la camara no se abre");
        g.mirrors_[1].filter = rgba(255, 70, 190);
        run(g, 0.1f);
        for (const Gate& gt : g.gates_) if (gt.id == "camara") vault = !gt.closed;
        CHECK(g.receptors_[0].lit && vault, "con luz magenta, si");
        // El jefe espera al guion: no aparece hasta examinar el puesto de Nora.
        skipDialogues(g);
        for (Encounter& e : g.encounters_)
            if (e.id != "jefe_nora") e.cleared = true;
        g.nodes_[0].lit = true;
        place(g, {0, 0, 44});
        run(g, 0.5f);
        skipDialogues(g);
        CHECK(g.enemies_.empty(), "el jefe no aparece antes de tiempo");
        place(g, g.interacts_[0].pos);
        g.update(dt, press(&Controls::interact));
        skipDialogues(g);
        place(g, {0, 0, 44});
        run(g, 0.5f);
        bool boss = g.enemies_.size() == 1 && g.enemies_[0].boss && g.enemies_[0].size > 1.4f;
        CHECK(boss, "tras examinar el puesto aparece «%s»", g.enemies_.empty() ? "-" : g.enemies_[0].name());
    }
    {  // Puente: sin suelo, caer devuelve al último farol encendido
        Game g;
        g.init(GAME_ASSET_DIR);
        skipDialogues(g);
        run(g, 2.0f);  // que acabe de levantarse
        g.loadLevel(LevelId::Puente);
        skipDialogues(g);
        CHECK(!g.world_.ground, "el puente no tiene suelo");
        place(g, g.lanterns_[0].pos + Vec3{-0.8f, 0.1f, 0});
        g.update(dt, press(&Controls::interact));
        CHECK(g.lanterns_[0].lit, "farol encendido");
        g.hero_.body.pos = {5, 0.5f, 6};  // fuera del tramo: cae
        run(g, 2.0f);
        CHECK(length(g.hero_.body.pos - g.checkpoint_) < 1.0f && g.hp_ < 100, "cae al vacío y vuelve al farol (PV %.0f)", g.hp_);
    }
    {  // Guion: escenas, avisos, recuerdos y órdenes
        Script sc;
        sc.parse("# comentario\n== a\nbrizna: Hola, ¿qué tal?\n!oleadas\nprota: …\n== b\n!aviso\nbrizna: Ojo.\n== c\n!recuerdo\nvoz: Te llamábamos…\n");
        const Scene* a = sc.find("a");
        CHECK(sc.scenes().size() == 3 && a && a->blocking && a->lines.size() == 3 && a->lines[1].command &&
                  a->lines[1].speaker == "oleadas",
              "guion: 3 escenas, la primera con 2 lineas y una orden");
        CHECK(!sc.find("b")->blocking && sc.find("c")->memory, "aviso no bloquea; recuerdo marcado");
        Dialogue d;
        d.start(a);
        std::vector<StoryLine> cmds;
        d.update(0.1f, cmds);
        CHECK(d.visibleChars() == 4 && !d.lineComplete(), "maquina de escribir: %d caracteres en 0,1 s", d.visibleChars());
        d.advance();
        CHECK(d.lineComplete() && utf8Prefix(d.line()->text, d.visibleChars()) == "Hola, ¿qué tal?",
              "pulsar completa la linea (UTF-8 intacto)");
        d.advance();
        d.update(0.01f, cmds);
        CHECK(cmds.size() == 1 && cmds[0].speaker == "oleadas" && d.line() && d.line()->speaker == "prota",
              "al pasar se entrega la orden y sigue la siguiente linea");
        d.advance();
        d.advance();
        CHECK(!d.active(), "la escena termina");
    }
    {  // Historia sin palabras: la intro son planos; los ecos son siluetas que repiten un recuerdo
        Game g;
        g.init(GAME_ASSET_DIR);
        CHECK(g.story_ && g.hero_.lying == 0 && !g.dialogueBlocking(), "con guion: se empieza de pie, sin intro");
        run(g, 0.5f);
        CHECK(!g.tutorials_.empty() && g.tutorials_.front().action == "andar",
              "el primer tutorial dice «%s»", g.tutorials_.empty() ? "-" : g.tutorials_.front().word.c_str());
        Controls walk;
        walk.move = {0, 0, 1};
        run(g, 2.5f, walk);
        CHECK(g.tutorials_.empty() || g.tutorials_.front().action != "andar" || g.tutorials_.front().doneTime >= 0,
              "al andar, el tutorial se da por hecho");
        g.enemies_.clear();
        // Eco de memoria: aparece con su orden y, al tocarlo, sus siluetas repiten el recuerdo.
        g.storyCommand({true, "eco_activa", "1"});
        g.hero_.body.pos = g.echoes_[0].pos - Vec3{0, 0.9f, 0};
        g.update(dt, {});
        g.update(dt, {});
        run(g, 0.5f);
        CHECK(g.echoes_[0].taken && g.ghosts_.size() == 2, "el eco lanza su recuerdo: %zu siluetas", g.ghosts_.size());
        float x0 = g.ghosts_[1].body.body.pos.x;
        run(g, 5.0f);
        CHECK(g.ghosts_.empty() || std::fabs(g.ghosts_.back().body.body.pos.x - x0) > 0.3f, "las siluetas se mueven y desaparecen");
    }
    {  // Ilse apaga el puente y despierta la sombra
        Game g;
        g.init(GAME_ASSET_DIR);
        skipDialogues(g);
        g.loadLevel(LevelId::Puente);
        skipDialogues(g);
        for (Lantern& l : g.lanterns_) l.lit = true;
        place(g, {0, 1.0f, 64});
        g.playScene("archivera");
        run(g, 1.0f);
        bool ilse = false;
        for (const Ghost& gh : g.ghosts_) ilse |= gh.ilse;
        CHECK(ilse, "aparece Ilse al otro lado del puente");
        skipDialogues(g);
        bool dark = true;
        for (const Lantern& l : g.lanterns_) dark = dark && !l.lit;
        for (const Lamp& l : g.lamps_) dark = dark && !l.on;
        CHECK(dark && g.shadowAt({0, 1.0f, 66}) > 0.9f && g.shadowUnlocked() && !g.portals_.empty(),
              "apaga todas las luces: queda la oscuridad, la sombra despierta y un portal");
        bool armed = false;
        for (const Encounter& e : g.encounters_) armed |= e.id == "sabuesos_ilse" && e.armed;
        CHECK(armed, "y suelta a su jauría");
    }
    {  // Sonido sintetizado: todos los efectos suenan, sin saturar, y el WAV va y vuelve
        bool allOk = true;
        for (int i = 0; i < int(Sfx::Count); ++i) {
            if (Sfx(i) == Sfx::Abuelo) continue;  // solo grabado
            StereoBuffer b = renderSfx(Sfx(i));
            int peak = 0;
            for (int16_t v : b.data) peak = std::max(peak, std::abs(int(v)));
            if (b.frames() < 100 || peak < 500 || peak >= 32000) allOk = false;
        }
        CHECK(allOk, "%d efectos de sonido con contenido y sin recortar", int(Sfx::Count));
        StereoBuffer a = renderSfx(Sfx::Chest), b;
        std::string path = "/tmp/unamed_game_test.wav";
        CHECK(saveWav(path, a) && loadWav(path, b) && a.data == b.data, "WAV: se guarda y se lee identico");
        std::remove(path.c_str());
    }
    {  // Modo prueba: Grises sin fin en un patio aleatorio, con vida y Luz infinitas
        Game g;
        g.initTest(TestMode::Combat, 7);
        int lit = 0;
        for (const ColorNode& n : g.nodes_) lit += n.lit;
        CHECK(g.level() == LevelId::Prueba && !g.story_ && g.lamps_.size() >= 4 && lit >= 1,
              "patio aleatorio: %zu farolas y %d nodos encendidos", g.lamps_.size(), lit);
        Game same, other;
        same.initTest(TestMode::Combat, 7);
        other.initTest(TestMode::Combat, 8);
        auto sig = [](const Game& x) {
            float s = 0;
            for (const Block& b : x.blocks_) s += b.box.min.x * 3.1f + b.box.max.z;
            return s + float(x.blocks_.size());
        };
        CHECK(sig(same) == sig(g) && sig(other) != sig(g), "la misma semilla da el mismo escenario; otra, otro");
        run(g, 6.0f);
        CHECK(g.enemies_.size() >= 3, "aparecen Grises solos (%zu)", g.enemies_.size());
        g.hurtHero({0, 0, 1}, 500, 2, nullptr);
        run(g, 0.1f);
        CHECK(g.hero_.downTimer < 0 && g.hp_ == g.maxHp_, "vida infinita: un golpe de 500 no derriba");
        g.mp_ = 0;
        run(g, 0.05f);
        CHECK(g.mp_ < 1.0f, "por defecto el Poder no es infinito (para probar su gestión)");
        g.setTestCheats(true, true);
        run(g, 0.05f);
        CHECK(g.mp_ == g.maxMp_, "con F2: Poder infinito");
        g.setTestCheats(false, false);
        g.hp_ = 50;
        run(g, 0.05f);
        CHECK(g.hp_ < g.maxHp_, "con F1 apagado la vida ya no se rellena");
        g.setTestCheats(true, false);
        for (int wave = 0; wave < 3; ++wave) {
            for (Enemy& e : g.enemies_)
                if (e.alive()) g.killEnemy(e);
            run(g, 5.0f);
        }
        int alive = 0;
        for (const Enemy& e : g.enemies_) alive += e.alive();
        CHECK(g.kills_ >= 9 && alive >= 3, "al disiparlos vuelven más (bajas %d, vivos %d)", g.kills_, alive);
        CHECK(g.inv_.count(ItemId::LightLance) > 0 && g.inv_.count(ItemId::EclipseScythe) > 0, "con armas de las dos mitades para probar");
    }
    {  // Enemigos nuevos: turnos de ataque, oportunistas por la espalda, botín de su familia
        Game g = fresh();
        place(g, {0, 0, -10});
        for (int i = 0; i < 5; ++i) {
            float a = float(i) * 1.25f;
            Enemy& e = addEnemy(g, EnemyKind::Dazzled, Vec3{std::cos(a) * 1.8f, 0, -10 + std::sin(a) * 1.8f}, AiState::Chase);
            e.intent = Intent::Attack;
            e.decisionTimer = 10;
        }
        int maxBusy = 0;
        for (int k = 0; k < 30; ++k) {
            g.update(dt, {});
            int busy = 0;
            for (const Enemy& e : g.enemies_) busy += e.state == AiState::Windup || e.state == AiState::Attack;
            maxBusy = std::max(maxBusy, busy);
        }
        CHECK(maxBusy >= 1 && maxBusy <= 2, "cinco Deslumbrados: como mucho dos atacan a la vez (%d)", maxBusy);
        Game h = fresh();
        place(h, {0, 0, -10}, 0);  // mirando a +z
        Enemy& hound = addEnemy(h, EnemyKind::Hound, {0, 0, -12}, AiState::Chase);
        hound.intent = Intent::Wait;
        hound.decisionTimer = 10;
        bool lunged = false;
        for (int k = 0; k < 20 && !lunged; ++k) {
            h.update(dt, {});
            lunged = h.enemies_[0].state == AiState::Windup || h.enemies_[0].state == AiState::Attack;
        }
        CHECK(lunged, "el Sabueso ataca por la espalda aunque su táctica fuera esperar");
        Game k = fresh();
        place(k, {0, 0, -10});
        int shadowOrbs = 0, lightOrbs = 0;
        for (int i = 0; i < 6; ++i) {
            k.enemies_.clear();
            k.pickups_.clear();
            Enemy& e = addEnemy(k, i % 2 ? EnemyKind::Hound : EnemyKind::Watcher, {3, 0, -10});
            k.killEnemy(e);
            for (const Pickup& p : k.pickups_) shadowOrbs += p.kind == PickupKind::Shadow, lightOrbs += p.kind == PickupKind::Mp;
        }
        CHECK(shadowOrbs > 0 && lightOrbs > 0, "orbes de Poder violetas de los de sombra (%d) y dorados de los de luz (%d)",
              shadowOrbs, lightOrbs);
        k.mp_ = 0;
        run(k, 3.0f);
        CHECK(k.mp_ > 0, "los dos suman a la misma barra de Poder (%.0f)", k.mp_);
    }
    {  // Niveles: los enemigos sueltan experiencia y vida; al subir, más vida y fuerza
        Game g = fresh();
        place(g, {0, 0, -10});
        g.hp_ = 50;
        Enemy& e = addEnemy(g, EnemyKind::Brute, {2, 0, -10});
        g.killEnemy(e);
        int xpOrbs = 0, hpOrbs = 0, xpTotal = 0;
        for (const Pickup& p : g.pickups_) {
            xpOrbs += p.kind == PickupKind::Xp, hpOrbs += p.kind == PickupKind::Hp;
            if (p.kind == PickupKind::Xp) xpTotal += p.value;
        }
        CHECK(xpOrbs > 0 && hpOrbs > 0, "un Coloso suelta experiencia (%d) y vida (%d orbes)", xpTotal, hpOrbs);
        run(g, 3.0f);
        CHECK(g.xp_ == xpTotal && g.hp_ > 50, "al recogerlo suma experiencia (%d) y vida (%.0f)", g.xp_, g.hp_);
        float hp0 = g.maxHp_, pow0 = g.levelPower();
        g.gainXp(Game::xpToNext(1) - g.xp_ + 5);
        CHECK(g.heroLevel() == 2 && g.xp_ == 5 && g.maxHp_ > hp0 && g.levelPower() > pow0 && g.hp_ == g.maxHp_,
              "sube a nivel 2: vida %.0f→%.0f, sobra la experiencia (%d)", hp0, g.maxHp_, g.xp_);
        g.gainXp(100000);
        CHECK(g.heroLevel() == Game::kMaxLevel, "el nivel tiene tope (%d)", g.heroLevel());
    }
    {  // La sombra: portales, oscuridad lejos de las farolas, fundirse y moverse por ella
        Game g = fresh();
        g.unlockShadow();
        g.update(dt, press(&Controls::swapMode));
        CHECK(!g.shadowMode(), "en la historia, Z no cambia de modo (se cambia en los portales)");
        g.addPortal({0, 0, -12});
        place(g, {0, 0, -14});
        Controls fwd;
        fwd.move = {0, 0, 1};
        run(g, 0.8f, fwd);
        CHECK(g.shadowMode(), "cruzar el portal: modo sombra");
        Vec3 lamp = g.lamps_[1].pos;  // farola encendida
        CHECK(g.shadowAt({lamp.x, 0, lamp.z}) < 0.1f && g.shadowAt({0, 0, -14}) > 0.9f,
              "junto a una farola encendida hay luz; lejos de ellas, oscuridad");
        place(g, {-2, 0, -14});
        g.mp_ = 50;
        run(g, 0.1f, press(&Controls::light));
        CHECK(g.diving(), "Q en la oscuridad: se funde con la sombra");
        float s0 = g.mp_;
        Controls go;
        go.move = {1, 0, 0};
        run(g, 1.0f, go);
        CHECK(g.mp_ < s0 && g.hero_.body.pos.x > 2.0f, "fundido se mueve rápido por la oscuridad y gasta Poder (%.0f)", g.mp_);
        // La luz no deja pasar: hacia la farola se queda en el borde de su luz.
        Controls toLamp;
        toLamp.move = normalize(flat(Vec3{lamp.x, 0, lamp.z} - g.hero_.body.pos));
        run(g, 3.0f, toLamp);
        CHECK(g.shadowAt(g.hero_.body.pos) > 0.4f && length(flat(g.hero_.body.pos - lamp)) > 2.0f, "la luz no le deja pasar");
        // Enemigos: fundido no le ven.
        Enemy& d = addEnemy(g, EnemyKind::Dazzled, g.hero_.body.pos + Vec3{3, 0, 0}, AiState::Idle);
        (void)d;
        run(g, 0.5f);
        CHECK(g.enemies_[0].state == AiState::Idle, "fundido con la sombra no le ven");
        // Salir en emboscada.
        run(g, 0.1f, press(&Controls::light));
        CHECK(!g.diving(), "Q otra vez: sale de la sombra");
        // Trepar como sombra: contra una pared a oscuras sube por ella y pasa al tejado.
        {
            g.enemies_.clear();
            std::vector<AABB> keep = g.world_.solids;  // a solas con la pared de prueba
            g.world_.solids = {AABB{{-2, 0, -24}, {2, 3, -21}}};
            place(g, {0, 0, -19});
            g.mp_ = 100;
            run(g, 0.1f, press(&Controls::light));
            Controls up;
            up.move = {0, 0, -1};
            run(g, 0.6f, up);
            CHECK(g.diving() && g.onWall() && g.hero_.body.pos.y > 0.5f, "fundido contra una pared oscura, sube por ella (y=%.1f)",
                  g.hero_.body.pos.y);
            for (int k = 0; k < 120 && g.onWall(); ++k) g.update(dt, up);  // hasta llegar arriba
            CHECK(g.diving() && !g.onWall() && std::fabs(g.hero_.body.pos.y - 3.0f) < 0.1f && g.hero_.body.pos.z < -21.0f,
                  "arriba pasa al tejado (y=%.1f)", g.hero_.body.pos.y);
            run(g, 0.5f);
            CHECK(std::fabs(g.hero_.body.pos.y - 3.0f) < 0.1f, "y se queda en el tejado");
            Controls back;
            back.move = {0, 0, 1};
            run(g, 1.0f, back);  // vuelve al borde y se deja caer
            run(g, 1.5f);
            CHECK(g.hero_.body.pos.y < 0.1f, "al salir del tejado cae al suelo (y=%.1f)", g.hero_.body.pos.y);
            g.world_.solids = keep;
            run(g, 0.1f, press(&Controls::light));
        }
        // Agujero negro.
        g.portals_.clear();
        place(g, {0, 0, -12}, 0);
        addEnemy(g, EnemyKind::Dazzled, {0, 0, -8.5f});
        addEnemy(g, EnemyKind::Hound, {1.5f, 0, -8});
        g.mp_ = 100;
        float h0 = g.enemies_[0].health + g.enemies_[1].health;
        run(g, 3.5f, command(4));
        float h1 = 0;
        for (const Enemy& e : g.enemies_) h1 += std::max(0.0f, e.health);
        CHECK(g.mp_ < 75 && h1 < h0 * 0.6f, "técnica 5 en la sombra: el agujero negro atrae y deshace (vida %.0f → %.0f)", h0, h1);
        // Golpes cruzados.
        g.enemies_.clear();
        addEnemy(g, EnemyKind::Dazzled, {0, 0, 10});
        addEnemy(g, EnemyKind::Brute, {3, 0, 10});
        float hd = g.enemies_[0].health, hb = g.enemies_[1].health;
        g.hitEnemy(g.enemies_[0], {1, 0, 0}, 10, 0, 0, false);
        g.hitEnemy(g.enemies_[1], {1, 0, 0}, 10, 0, 0, false);
        CHECK(hd - g.enemies_[0].health > hb - g.enemies_[1].health, "en modo sombra se hiere más a los de luz");
    }
    {  // Técnicas de cada mitad: la luz y la sombra tienen las suyas en el menú de comandos
        Game g = fresh();
        g.unlockShadow();
        g.portals_.clear();
        place(g, {0, 0, -12}, kPi);
        CHECK(modeSpell(false, 3) == Spell::Burst && modeSpell(true, 0) == Spell::Spikes, "cada mitad, sus cinco técnicas");
        addEnemy(g, EnemyKind::Brute, {0, 0, -16});
        g.mp_ = 100;
        run(g, 2.0f, command(3));
        CHECK(g.enemies_[0].health < kBruteHp - 15, "Estallido solar: la bola estalla en el Coloso (%.0f)", g.enemies_[0].health);
        g.shadowMode_ = true;
        g.enemies_.clear();
        Enemy& d = addEnemy(g, EnemyKind::Dazzled, {0, 0, -16});
        float hd = d.health;
        g.mp_ = 100;
        run(g, 0.6f, command(0));
        CHECK(g.enemies_[0].health < hd && g.enemies_[0].body.pos.y > 0.3f, "Púas de sombra: hieren y lanzan por los aires");
        g.enemies_.clear();
        for (int i = 0; i < 3; ++i) addEnemy(g, EnemyKind::Dazzled, Vec3{std::cos(float(i) * 2.0f) * 4, 0, -12 + std::sin(float(i) * 2.0f) * 4});
        run(g, 0.6f, command(1));
        run(g, 1.2f);
        bool held = true;
        for (const Enemy& e : g.enemies_) held = held && e.state == AiState::Stagger;
        CHECK(held, "Cadenas del vacío: los de alrededor se quedan quietos un buen rato");
        g.hp_ = 40;
        run(g, 0.6f, command(2));
        CHECK(g.hp_ > 40, "Drenar: roba vida (%.0f PV)", g.hp_);
        g.enemies_.clear();
        g.hp_ = g.maxHp_;
        run(g, 1.0f);  // que se recupere si le han golpeado
        addEnemy(g, EnemyKind::Brute, {0, 0, -15});
        g.mp_ = 100;
        run(g, 2.0f, command(3));
        CHECK(g.enemies_[0].health < kBruteHp - 25, "Mina oscura: implosiona sobre el Coloso (%.0f)", g.enemies_[0].health);
    }
    {  // Niveles de noche: cada uno con su tipo de luz, oscuridad de sobra y sus combates
        const LevelId ids[] = {LevelId::Selva, LevelId::Cueva, LevelId::Callejones};
        const LampStyle styles[] = {LampStyle::Torch, LampStyle::Crystal, LampStyle::Paper};
        for (int k = 0; k < 3; ++k) {
            Game g;
            g.initLevel(ids[k]);
            run(g, 1.0f);
            int ofStyle = 0;
            for (const Lamp& l : g.lamps_) ofStyle += l.style == styles[k];
            int dark = 0, n = 0;
            for (float x = -8; x <= 8; x += 2)
                for (float z = 0; z <= 40; z += 2, ++n) dark += g.shadowAt({x, g.hero_.body.pos.y, z}) > 0.6f;
            CHECK(g.level() == ids[k] && ofStyle >= 3 && g.night_ > 0.5f && g.partCount() >= 2 && !g.encounters_.empty(),
                  "%s: de noche, %d luces de su estilo, %d partes", g.levelName().c_str(), ofStyle, g.partCount());
            CHECK(dark > n / 3 && dark < n, "%s: hay luz y oscuridad (%d de %d puntos a oscuras)", g.levelName().c_str(), dark, n);
            CHECK(g.hero_.body.grounded && g.hero_.body.pos.y > -1.0f, "%s: se empieza de pie en el suelo", g.levelName().c_str());
        }
    }
    {  // Farolas: romperlas a golpes crea sombra para siempre; los interruptores las apagan y encienden
        Game g = fresh();
        g.unlockShadow();
        g.portals_.clear();
        g.shadowMode_ = true;
        Vec3 lamp = g.lamps_[1].pos;
        place(g, {lamp.x, 0, lamp.z + 1.2f}, kPi);  // al lado del poste, mirándolo
        CHECK(g.shadowAt(g.hero_.body.pos) < 0.2f, "junto a la farola encendida hay luz");
        g.mp_ = 50;
        run(g, 0.1f, press(&Controls::light));
        CHECK(!g.diving(), "con la farola encendida no se puede fundir");
        for (int i = 0; i < 6 && !g.lamps_[1].broken; ++i) {
            run(g, 0.05f, press(&Controls::attack));
            run(g, 0.6f);
        }
        CHECK(g.lamps_[1].broken && !g.lamps_[1].on, "a golpes la farola se rompe");
        CHECK(g.shadowAt(g.hero_.body.pos) > 0.9f, "rota, ahí hay sombra");
        run(g, 0.1f, press(&Controls::light));
        CHECK(g.diving(), "y ya se puede fundir en ella");
        g.diving_ = false;
        CHECK(!g.switches_.empty(), "hay interruptores (%zu)", g.switches_.size());
        int si = -1;
        for (size_t i = 0; i < g.switches_.size(); ++i)
            for (int li : g.switches_[i].lamps)
                if (li == 1) si = int(i);
        CHECK(si >= 0, "la farola rota tiene su interruptor");
        const LampSwitch sw = g.switches_[size_t(si)];
        CHECK(length(flat(sw.pos - lamp)) > 5.0f, "el interruptor está apartado de sus farolas (%.1f m)", length(flat(sw.pos - lamp)));
        place(g, sw.pos + Vec3{0.8f, 0, 0});
        run(g, 0.1f, press(&Controls::interact));
        bool othersOff = true;
        for (int li : sw.lamps) othersOff = othersOff && !g.lamps_[size_t(li)].on;
        CHECK(!g.switches_[size_t(si)].on && othersOff, "el interruptor apaga su grupo de farolas");
        run(g, 0.5f);
        run(g, 0.1f, press(&Controls::interact));
        bool othersOn = true;
        for (int li : sw.lamps) othersOn = othersOn && (g.lamps_[size_t(li)].on || g.lamps_[size_t(li)].broken);
        CHECK(othersOn && g.lamps_[1].broken && !g.lamps_[1].on, "al encenderlas vuelven todas menos la rota");
    }
    {  // La luz se gasta: saltar de farola en farola cuesta Luz y apaga las farolas que se usan
        Game g = fresh();
        Vec3 lampA = g.lamps_[1].pos, lampB = g.lamps_[2].pos;
        place(g, {lampA.x, 0, lampA.z - 2.0f});
        g.mp_ = 50;
        run(g, 1.0f, press(&Controls::light));
        CHECK(g.insideLight() && g.mp_ < 60, "entrar cuesta Luz (queda %.0f tras recargar)", g.mp_);
        float e0 = g.lamps_[1].energy;
        run(g, 2.0f);
        CHECK(g.lamps_[1].energy < e0, "dentro, la farola se va gastando (%.2f)", g.lamps_[1].energy);
        Controls aim;
        aim.aimOrigin = lampA + Vec3{3, 0.5f, -2};
        aim.aimDir = normalize(lampB - aim.aimOrigin);
        g.update(dt, aim);
        aim.light = true;
        float before = g.mp_;
        run(g, 1.0f, aim);
        CHECK(g.insideLight() && length(g.hostLight() - lampB) < 0.01f && g.mp_ < before + 12 && g.lamps_[1].energy < e0 - 0.4f,
              "saltar cuesta Luz y la farola que deja atrás pierde luz (%.2f)", g.lamps_[1].energy);
        g.mp_ = 2;
        Controls back = aim;
        back.aimOrigin = lampB + Vec3{-3, 0.5f, -2};
        back.aimDir = normalize(lampA - back.aimOrigin);
        g.lamps_[2].energy = 0.01f;  // la de dentro, sin luz que dar
        run(g, 1.0f, back);
        CHECK(length(g.hostLight() - lampA) > 0.1f || !g.insideLight(), "sin Luz no se puede saltar");
    }
    {  // Un jefe que llama a su jauría en mitad del combate (antes corrompía la memoria)
        Game g = fresh();
        place(g, {0, 0, -12});
        for (int i = 0; i < 3; ++i) addEnemy(g, EnemyKind::Dazzled, {float(i) * 2 - 2, 0, -4}, AiState::Chase);
        g.spawnEnemy({EnemyKind::Brute, {0, 0, -6}, true, "Prueba"});
        g.enemies_.back().state = AiState::Chase;
        g.enemies_.back().health = enemyDef(EnemyKind::Brute).maxHealth * 7.0f * 0.3f;  // de golpe bajo 1/3
        run(g, 0.5f);
        int hounds = 0;
        for (const Enemy& e : g.enemies_) hounds += e.kind == EnemyKind::Hound;
        CHECK(hounds == 2, "el jefe de sombra llama a dos sabuesos (%d) sin romper nada", hounds);
    }
    {  // El modo prueba sirve también para la luz y la sombra: portales, oscuridad y farolas
        Game g;
        g.initTest(TestMode::Combat, 11);
        float darkArea = 0;
        for (int i = 0; i < 100; ++i) darkArea += g.shadowAt({float(i % 10) * 3.6f - 18, 0, float(i / 10) * 3.6f - 18}) > 0.6f;
        CHECK(g.portals_.size() == 2 && g.lamps_.size() >= 6 && darkArea > 30 && darkArea < 95,
              "patio de prueba: %zu portales, %zu farolas y un %.0f%% a oscuras", g.portals_.size(), g.lamps_.size(), darkArea);
        g.update(dt, press(&Controls::swapMode));
        CHECK(g.shadowMode(), "en la prueba Z cambia de modo");
    }
    {  // Derrota y vuelta a empezar
        Game g = fresh();
        g.hurtHero({0, 0, 1}, 500, 2, nullptr);
        CHECK(g.hero_.downTimer >= 0, "derrotado con 0 PV");
        run(g, 3.2f);
        CHECK(g.hero_.downTimer < 0 && g.hp_ == 100 && length(g.hero_.body.pos - g.heroStart_) < 0.1f,
              "reaparece en el inicio con la vida llena");
    }
    {  // Voz: el remate grita con la voz de la mitad activa (Vela en la luz, Hollín en la sombra)
        auto shout = [](Game& g, Sfx want) {
            g.takeSounds();
            g.voiceCooldown_ = 0;
            g.hero_.forceMove(Move::Finisher, 0, 0);
            g.update(dt, {});
            bool heard = false;
            for (const SoundEvent& e : g.takeSounds()) heard |= e.id == want;
            return heard;
        };
        Game g = fresh();
        g.unlockShadow();
        CHECK(shout(g, Sfx::VelaFinisher), "en la luz, el remate suena con la voz de Vela");
        run(g, 1.5f);
        g.freePlay_ = true;
        g.update(dt, press(&Controls::swapMode));
        run(g, 1.0f);
        CHECK(g.shadowMode() && shout(g, Sfx::HollinFinisher), "en la sombra, con la de Hollín");
    }
    {  // Voces grabadas (assets/audio/voces): tomas, huecos cubiertos y sin mezclar con las sintéticas
        std::string dir = std::string(GAME_ASSET_DIR) + "/audio/voces";
        StereoBuffer w;
        CHECK(loadAnyWav(dir + "/vela_golpe_1.wav", w) && w.frames() > kSampleRate / 10, "se lee un WAV en coma flotante (%zu muestras)",
              w.frames());
        CHECK(sfxVariants(Sfx::VelaAttack1, dir).size() == 3 && sfxVariants(Sfx::HollinHurt, dir).size() == 3,
              "tres tomas de golpe de Vela y de dolor de Hollín");
        CHECK(sfxVariants(Sfx::VelaFinisher, dir).size() == 3 && sfxVariants(Sfx::HollinDodge, dir).size() == 3,
              "el remate sale de los golpes y la esquiva de los saltos");
        CHECK(sfxVariants(Sfx::HollinLaugh, dir).empty(), "la risa, sin grabar, se calla (no suena sintética)");
        CHECK(sfxVariants(Sfx::VelaAttack1, "/no/existe").size() == 1, "sin grabaciones, la voz sintetizada");
        std::vector<StereoBuffer> barks = sfxVariants(Sfx::Abuelo, dir);
        int full = 0;
        for (const StereoBuffer& b : barks) full += b.frames() > kSampleRate / 10;
        CHECK(barks.size() == 45 && full == 45, "barks del abuelo: %d de 45 tomas leídas", full);
        CHECK(sfxVariants(Sfx::Abuelo, "/no/existe").empty(), "sin los barks del abuelo, se calla");
    }
    {  // Al hablar con el abuelo, entra con un bark: saluda la primera vez y luego va variando
        Game g = fresh();
        g.loadLevel(LevelId::Plaza);
        Npc* abuelo = nullptr;
        for (Npc& n : g.npcs_)
            if (n.model == "abuelo") abuelo = &n;
        CHECK(abuelo != nullptr, "el abuelo está en la plaza");
        if (abuelo) {
            auto barkKind = [&] {
                int k = -1;
                for (const SoundEvent& e : g.takeSounds())
                    if (e.id == Sfx::Abuelo) k = e.take / kBarkTakes;
                return k;
            };
            g.takeSounds();
            g.talkTo(*abuelo);
            int first = barkKind();
            g.talkTo(*abuelo);
            int second = barkKind();
            CHECK(first == int(BarkKind::Saludo) && second >= 0 && second != int(BarkKind::Saludo),
                  "abuelo: saluda al hablarle (%d) y luego otro bark (%d)", first, second);
        }
    }
    {  // Personajes con esqueleto en .glb (enemigos y lobo): armadura con los huesos del juego y pesos
        struct Char {
            const char* file;
            EnemyKind kind;
        };
        for (Char ch : {Char{"deslumbrado", EnemyKind::Dazzled}, Char{"coloso", EnemyKind::Brute}, Char{"vigia", EnemyKind::Watcher},
                        Char{"sabueso", EnemyKind::Hound}, Char{"lobo_de_sombra", EnemyKind::Hound}, Char{"cangrejo", EnemyKind::Crab}}) {
            GlbFile f;
            std::string err;
            bool ok = f.load(std::string(GAME_ASSET_DIR) + "/models/" + ch.file + ".glb", err);
            const EnemyDef& d = enemyDef(ch.kind);
            bool bones = ok && f.skins.size() == 1 && int(f.skins[0].joints.size()) == d.jointCount;
            for (int k = 0; bones && k < d.jointCount; ++k) bones = f.find(d.rig[k].name) >= 0;
            float worst = 0;
            size_t verts = 0;
            for (const auto& mesh : f.meshes)
                for (const GlbPrimitive& p : mesh) {
                    verts += p.positions.size();
                    for (size_t i = 0; i + 3 < p.weights.size(); i += 4)
                        worst = std::max(worst, std::fabs(p.weights[i] + p.weights[i + 1] + p.weights[i + 2] + p.weights[i + 3] - 1.0f));
                    bones = bones && p.colors.size() == p.positions.size() * 4;
                }
            CHECK(bones && verts > 1000 && worst < 0.02f, "%s.glb: %zu vértices, armadura de %d huesos, colores y pesos (error %.3f) %s",
                  ch.file, verts, d.jointCount, worst, err.c_str());
        }
    }
    {  // Bestiario: los esqueletos salen de los .glb (padres antes que hijos, cada hueso en su sitio)
        const std::string dir = std::string(GAME_ASSET_DIR) + "/models/bestiario/";
        int n = loadCreatureRigs(dir);
        CHECK(n == int(CreatureKind::Count), "bestiario: %d de %d esqueletos leídos", n, int(CreatureKind::Count));
        bool order = true, placed = true;
        for (int k = 0; k < int(CreatureKind::Count); ++k) {
            const CreatureRig& rig = creatureRig(CreatureKind(k));
            GlbFile f;
            std::string err;
            f.load(dir + creatureFile(CreatureKind(k)) + ".glb", err);
            Mat4 w[kMaxJoints];
            computeWorldMatrices(rig.joints, rig.count, Pose{}, Mat4::identity(), w);
            for (int i = 0; i < rig.count; ++i) {
                order &= rig.joints[i].parent < i && rig.joints[i].name == rig.names[i].c_str();
                int node = f.find(rig.names[i]);
                placed &= node >= 0 && length(translationOf(w[i]) - translationOf(f.nodes[size_t(node)].world)) < 1e-3f;
            }
        }
        CHECK(order && placed, "bestiario: jerarquía ordenada (%d) y huesos donde los pone el .glb (%d)", order, placed);
        const CreatureRig& giant = creatureRig(CreatureKind::Larguero);
        CHECK(giant.count == JointCount && creatureRig(CreatureKind::Hollinete).find("tail2") >= 0 && creatureRig(CreatureKind::Ballena).find("jaw") >= 0,
              "bestiario: el Larguero con los %d huesos humanos, el gato con su cola, la ballena con su mandíbula", giant.count);
    }
    {  // Cangrejo de cristal: de frente, el caparazón para casi todo y ni se inmuta; por detrás o
        // con un remate, entra todo
        Game g = fresh();
        g.loadLevel(LevelId::Prueba);
        g.enemies_.clear();
        g.devNoSpawns_ = true;
        place(g, {0, 0, -4});
        addEnemy(g, EnemyKind::Crab, {0, 0, 0});  // mira hacia -z (hacia Vela)
        Enemy& e = g.enemies_.back();
        float h0 = e.health;
        g.hitEnemy(e, {0, 0, 1}, 10, 3, 0, false);  // de frente
        float front = h0 - e.health;
        bool unmoved = e.state != AiState::Stagger;
        float h1 = e.health;
        g.hitEnemy(e, {0, 0, -1}, 10, 3, 0, false);  // por detrás
        float back = h1 - e.health;
        e.state = AiState::Idle;
        float h2 = e.health;
        g.hitEnemy(e, {0, 0, 1}, 10, 3, 0, true);  // remate de frente
        float heavy = h2 - e.health;
        CHECK(front > 0 && front < back * 0.5f && unmoved && heavy > back * 0.9f,
              "cangrejo: de frente %.1f (sin tambalearse: %d), por detrás %.1f, remate de frente %.1f", front, unmoved, back, heavy);
        // Lejos, se acerca de lado; de cerca, encara.
        Game h = fresh();
        h.loadLevel(LevelId::Prueba);
        h.enemies_.clear();
        h.devNoSpawns_ = true;
        place(h, {0, 0, -14});
        addEnemy(h, EnemyKind::Crab, {0, 0, 0}, AiState::Chase);
        run(h, 0.6f);
        const Enemy& c = h.enemies_.back();
        Vec3 to = flat(h.hero_.body.pos - c.body.pos);
        float side = std::fabs(dot(normalize(to), c.forward()));
        CHECK(side < 0.5f && length(to) < 14.0f, "cangrejo: se acerca de lado (|cos| %.2f)", side);
    }
    {  // Criaturas en los escenarios: la Mechita sigue a Vela, el búho cambia de farola si se la
        // apagan, el gato salta de tejado si se le acercan y los comerciantes abren el taller
        Game g = fresh();
        g.loadLevel(LevelId::Plaza);
        auto find = [](Game& gg, CreatureKind k) -> Creature* {
            for (Creature& c : gg.creatures_)
                if (c.kind == k) return &c;
            return nullptr;
        };
        Creature* mechita = find(g, CreatureKind::Mechita);
        Creature* owl = find(g, CreatureKind::Buho);
        Creature* cat = find(g, CreatureKind::Hollinete);
        CHECK(mechita && owl && cat && find(g, CreatureKind::Ballena), "plaza: la Mechita, un búho, un gato en un tejado y la ballena en el cielo");
        if (mechita && owl && cat) {
            skipDialogues(g);
            place(g, g.heroStart_ + Vec3{6, 0, 0});
            Controls walk;
            walk.move = {0, 0, 1};
            run(g, 1.5f, walk);
            run(g, 1.5f);
            float gap = length(flat(mechita->pos - g.hero_.body.pos));
            CHECK(gap < 1.6f, "la Mechita va con Vela (a %.1f m)", gap);
            int lamp = owl->lamp;
            g.lamps_[size_t(lamp)].on = false;
            run(g, 6.0f);
            CHECK(owl->lamp != lamp && owl->lamp >= 0 && g.lamps_[size_t(owl->lamp)].on && owl->hop < 0 &&
                      length(owl->pos - g.lamps_[size_t(owl->lamp)].pos) < 0.6f,
                  "búho: le apagan la farola y vuela a otra encendida");
            g.hero_.body.pos = cat->pos + Vec3{1.0f, 0, 0};
            Vec3 before = cat->pos;
            run(g, 0.2f);
            bool jumped = cat->hop >= 0;
            run(g, 3.0f);
            CHECK(jumped && length(cat->pos - before) > 3.0f, "gato: salta a otro tejado si se le acercan (%.1f m)", length(cat->pos - before));
        }
        Game m = fresh();
        m.loadLevel(LevelId::Mercado);
        Creature* snail = find(m, CreatureKind::Caracol);
        Game c = fresh();
        c.loadLevel(LevelId::Cueva);
        Creature* mole = find(c, CreatureKind::Topo);
        bool shops = snail && mole;
        if (shops) {
            m.talkToCreature(*snail);
            c.talkToCreature(*mole);
            shops = m.takeShopRequest() && c.takeShopRequest();
        }
        CHECK(shops, "el Caracol mercader (mercado) y el Topo minero (cueva) abren el taller");
    }
    {  // El Larguero vuelve a encender una farola que se ha apagado (no la rota)
        Game g = fresh();
        g.loadLevel(LevelId::Campo);
        g.wildSpawns_ = false;
        g.enemies_.clear();
        Creature* giant = nullptr;
        for (Creature& c : g.creatures_)
            if (c.kind == CreatureKind::Larguero) giant = &c;
        int lamp = -1;
        float best = 1e9f;
        for (size_t i = 0; giant && i < g.lamps_.size(); ++i) {
            float d = length(flat(g.lamps_[i].pos - giant->pos));
            if (g.lamps_[i].bulb && !g.lamps_[i].broken && d < best && !g.lampSwitchedOff(int(i))) best = d, lamp = int(i);
        }
        CHECK(giant && lamp >= 0, "campo: el Larguero y una farola cerca (a %.0f m)", best);
        if (giant && lamp >= 0) {
            skipDialogues(g);
            g.lamps_[size_t(lamp)].on = false;
            bool lit = false;
            for (int s = 0; s < 90 && !lit; ++s) {
                run(g, 1.0f);
                lit = g.lamps_[size_t(lamp)].on;
            }
            CHECK(lit, "el Larguero vuelve a encender la farola apagada (%.0f m)", best);
        }
    }
    {  // Modelos de la protagonista (.glb): se leen y tienen la jerarquía de la ficha
        for (const char* name : {"vela", "hollin"}) {
            GlbFile f;
            std::string err;
            bool ok = f.load(std::string(GAME_ASSET_DIR) + "/models/" + name + ".glb", err);
            CHECK(ok && f.meshes.size() > 100, "%s.glb se lee: %zu mallas %s", name, f.meshes.size(), err.c_str());
            bool bones = true;
            for (const char* b : {"Hips", "Spine", "Neck", "Head", "UpperArm_L", "LowerArm_R", "Hand_R", "UpperLeg_L", "Foot_R"})
                bones &= f.find(b) >= 0;
            CHECK(bones && (f.find("Prop_Sword") >= 0 || f.find("Prop_Hammer") >= 0), "%s.glb: huesos de la ficha y su arma", name);
            // El esqueleto del juego tiene las articulaciones donde las tiene el modelo.
            Mat4 rest[JointCount];
            computeWorldMatrices(Pose{}, Mat4::identity(), rest);
            Vec3 knee = translationOf(f.nodes[size_t(f.find("LowerLeg_L"))].world), head = translationOf(f.nodes[size_t(f.find("Head"))].world);
            CHECK(std::fabs(knee.y - translationOf(rest[KneeL]).y) < 0.01f && length(head - translationOf(rest[Head])) < 0.01f,
                  "%s: rodilla y cabeza en su sitio del esqueleto", name);
        }
    }
    std::printf("\n%s (%d fallos)\n", fails ? "HAY FALLOS" : "TODO OK", fails);
    return fails ? 1 : 0;
}
