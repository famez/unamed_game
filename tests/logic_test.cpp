// Test de la lógica del juego, sin ventana ni OpenGL: se rellena `Controls` a mano, se
// simulan pasos fijos de 1/60 s (determinista) y se comprueba el estado interno.
// Se compila con AddressSanitizer + UBSan. Ejecutar: ctest --test-dir build  (o build/logic_test)
#define private public  // solo para el test: acceso al estado interno
#include "game/game.h"
#undef private
#include "audio/synth.h"
#include "render/glb.h"
#include <algorithm>
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

int main() {
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
    }
    {  // Personajes con esqueleto en .glb (enemigos y lobo): armadura con los huesos del juego y pesos
        struct Char {
            const char* file;
            EnemyKind kind;
        };
        for (Char ch : {Char{"deslumbrado", EnemyKind::Dazzled}, Char{"coloso", EnemyKind::Brute}, Char{"vigia", EnemyKind::Watcher},
                        Char{"sabueso", EnemyKind::Hound}, Char{"lobo_de_sombra", EnemyKind::Hound}}) {
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
