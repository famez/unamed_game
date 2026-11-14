// Test de la lógica del juego, sin ventana ni OpenGL: se rellena `Controls` a mano, se
// simulan pasos fijos de 1/60 s (determinista) y se comprueba el estado interno.
// Se compila con AddressSanitizer + UBSan. Ejecutar: ctest --test-dir build  (o build/logic_test)
#define private public  // solo para el test: acceso al estado interno
#include "game.h"
#undef private
#include "synth.h"
#include <cstdio>

const float dt = 1.0f / 60;
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
        c.jump = c.attack = c.dodge = c.interact = c.lockOn = c.unlock = c.runCommand = c.beam = c.nova = c.transform = c.summon = false;
        c.command = -1;
        c.commandScroll = 0;
    }
}
Controls press(bool Controls::*flag) {
    Controls c;
    c.*flag = true;
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
        Enemy& e = addEnemy(g, EnemyKind::Shroom, {0, 0, -8.6f});
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
        float dealt = 60 - g.enemies_[0].health;
        CHECK(std::fabs(dealt - 54) < 1e-3f, "dano del combo: %.0f (10+10+12+22 con la espada de madera)", dealt);
    }
    {  // Un combo completo derrota a un duende, que suelta dinero y orbes
        Game g = fresh();
        place(g, {0, 0, -10});
        addEnemy(g, EnemyKind::Imp, {0, 0, -8.8f});
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
        addEnemy(g, EnemyKind::Shroom, {0, 0, -8.0f});
        run(g, 0.3f, press(&Controls::attack));
        float moved = g.hero_.body.pos.z + 12;
        CHECK(moved > 1.5f, "golpe a 4 m: se acerca %.2f m", moved);
        CHECK(g.enemies_[0].health < 60, "y alcanza a la seta (vida %.0f)", g.enemies_[0].health);
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
        addEnemy(g, EnemyKind::Imp, {0, 0, -5}, AiState::Chase);
        run(g, 3.0f);
        CHECK(g.hp_ < 100, "el duende persigue y ataca: PV %.0f", g.hp_);

        Game r = fresh();
        place(r, {0, 0, -10});
        Enemy& e = addEnemy(r, EnemyKind::Imp, {0, 0, -8.2f}, AiState::Chase);
        e.yaw = kPi;
        r.enemyStartAttack(e);
        Controls d = press(&Controls::dodge);
        d.move = {1, 0, 0};
        run(r, 0.5f, d);
        CHECK(r.hp_ == 100, "la voltereta lo esquiva: PV %.0f", r.hp_);

        Game b = fresh();
        place(b, {0, 0, -10});
        Enemy& eb = addEnemy(b, EnemyKind::Imp, {0, 0, -8.6f}, AiState::Chase);
        Controls guard;
        guard.guard = true;
        run(b, 0.1f, guard);  // guardia recién levantada: bloqueo perfecto
        b.enemyStartAttack(eb);
        run(b, 0.3f, guard);
        CHECK(b.hp_ == 100, "guardia de frente: sin dano (PV %.0f)", b.hp_);
        CHECK(b.enemies_[0].state == AiState::Stagger, "bloqueo perfecto: el duende se tambalea (%s)",
              aiStateName(b.enemies_[0].state));
    }
    {  // Seta: la onda al caer se esquiva saltando
        Game g = fresh();
        place(g, {0, 0, -10});
        Enemy& e = addEnemy(g, EnemyKind::Shroom, {0, 0, -8.0f}, AiState::Chase);
        g.enemyStartAttack(e);
        run(g, 1.5f);
        float hit = g.hp_;
        Game j = fresh();
        place(j, {0, 0, -10});
        Enemy& ej = addEnemy(j, EnemyKind::Shroom, {0, 0, -8.0f}, AiState::Chase);
        j.enemyStartAttack(ej);
        run(j, 0.5f);
        j.update(dt, press(&Controls::jump));
        run(j, 1.0f);
        CHECK(hit < 100 && j.hp_ == 100, "onda de la seta: quieto %.0f PV, saltando %.0f PV", hit, j.hp_);
    }
    {  // Técnicas de luz: la lanza gasta Luz y daña; Calidez cura; sin Luz no salen
        Game g = fresh();
        place(g, {0, 0, -10});
        addEnemy(g, EnemyKind::Shroom, {0, 0, -5});
        float l0 = g.mp_;
        Controls c;
        c.command = 0;
        run(g, 1.5f, c);
        CHECK(std::fabs(g.mp_ - (l0 - 20)) < 1e-3f, "la lanza de luz gasta 20 de Luz (queda %.0f)", g.mp_);
        CHECK(g.enemies_[0].health < 60, "y daña a la seta (vida %.0f)", g.enemies_[0].health);
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
    {  // La Luz: absorber, hacerse luz y Supernova
        Game g = fresh();
        place(g, {0, 0, -10});
        Controls hold;
        hold.absorb = true;
        float l0 = g.mp_;
        run(g, 1.0f, hold);
        CHECK(g.mp_ == l0, "en una zona gris no hay luz que absorber");
        g.nodes_[0].lit = true;
        g.nodes_[0].litTime = 10;
        place(g, g.nodes_[0].pos + Vec3{1.5f, 0, 0});
        g.mp_ = 0;
        run(g, 1.0f, hold);
        CHECK(g.mp_ > 30 && g.hero_.body.pos.y > -0.01f, "junto a un nodo encendido se absorbe (%.0f de Luz en 1 s)", g.mp_);
        // Haz: gratis dentro de la luz y llega más lejos; fuera cuesta 20.
        Vec3 p0 = g.hero_.body.pos;
        float l1 = g.mp_;
        Controls beam = press(&Controls::beam);
        beam.camForward = {0, 0, 1};
        run(g, 0.8f, beam);
        float lit = length(flat(g.hero_.body.pos - p0));
        CHECK(g.mp_ >= l1 && lit > 11.0f, "haz dentro de la luz: gratis y recorre %.1f m", lit);
        place(g, {0, 0, -12});
        g.mp_ = 50;
        p0 = g.hero_.body.pos;
        addEnemy(g, EnemyKind::Imp, {0, 0, -7}, AiState::Idle);
        run(g, 0.8f, beam);
        float grey = length(flat(g.hero_.body.pos - p0));
        CHECK(g.mp_ == 30 && grey > 8.0f && grey < 11.5f, "haz en zona gris: cuesta 20 y recorre %.1f m", grey);
        CHECK(g.enemies_[0].health < 30, "el haz atraviesa al Rastrero y le hace dano (vida %.0f)", g.enemies_[0].health);
        CHECK(g.hp_ == 100, "durante el haz no te alcanzan");
        // Arrancar el color a un Gris aturdido.
        Enemy& e = g.enemies_[0];
        e.state = AiState::Stagger;
        e.stateTime = 0;
        place(g, e.body.pos + Vec3{1.5f, 0, 0});
        g.mp_ = 0;
        float hpE = e.health;
        e.state = AiState::Stagger;
        run(g, 0.5f, hold);
        CHECK(g.mp_ > 3 && (g.enemies_.empty() || g.enemies_[0].health < hpE), "absorber de un Gris aturdido: le arranca su color (+%.0f de Luz)", g.mp_);
        // Supernova: solo con la Luz llena.
        g.enemies_.clear();
        for (int i = 0; i < 4; ++i) addEnemy(g, EnemyKind::Shroom, g.hero_.body.pos + Vec3{std::cos(float(i)) * 3, 0, std::sin(float(i)) * 3});
        g.mp_ = 50;
        run(g, 0.5f, press(&Controls::nova));
        CHECK(g.enemies_[0].health == 60, "sin la Luz llena no hay Supernova");
        g.mp_ = 100;
        float c0 = g.croma_;
        run(g, 1.0f, press(&Controls::nova));
        bool all = true;
        for (const Enemy& en : g.enemies_) all = all && en.health < 30;
        CHECK(all && g.mp_ == 0 && g.croma_ > c0, "Supernova: dana a todos alrededor, gasta la Luz y devuelve croma");
    }
    {  // Hielo congela: el enemigo va más lento
        Game g = fresh();
        place(g, {0, 0, -10});
        addEnemy(g, EnemyKind::Shroom, {0, 0, -6});
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
        c.command = 3;  // atajo 4: pocion
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
        g.inventory().add(ItemId::Sharpener, 3);
        for (int i = 0; i < 3; ++i) g.useItem(ItemId::Sharpener);
        CHECK(g.inventory().powerBonus == 6 && g.inventory().count(ItemId::Sharpener) == 2, "el filo se queda en +6");
    }
    {  // Aprendizaje por refuerzo de los Grises
        Brain b;
        b.epsilon = 0;
        int st = Brain::state({1.5f, 0, 1.0f, true});
        for (int i = 0; i < 30; ++i) b.learn(st, Intent::Attack, 1.0f, st, true);
        b.learn(st, Intent::Wait, -0.5f, st, true);
        CHECK(b.q(st, Intent::Attack) > 0.9f && b.choose(st, 0.5f, 0.5f) == Intent::Attack,
              "Q-learning: una tactica premiada pasa a ser la elegida (Q=%.2f)", b.q(st, Intent::Attack));
        int guarding = Brain::state({1.5f, 2, 1.0f, true});
        CHECK(guarding != st, "ver al Prismero en guardia es otra situacion distinta");
        Brain c;
        std::string path = "/tmp/unamed_game_test.q";
        CHECK(b.save(path) && c.load(path) && c.q(st, Intent::Attack) == b.q(st, Intent::Attack) && c.experience() == 31,
              "el cerebro se guarda y se carga igual");
        std::remove(path.c_str());

        Game g = fresh();
        place(g, {0, 0, -10});
        addEnemy(g, EnemyKind::Imp, {0, 0, -7}, AiState::Chase);
        run(g, 12.0f);
        const Brain& imp = g.brain(int(EnemyKind::Imp));
        float bestAttack = -1e9f;
        for (int s2 = 0; s2 < Brain::kStates; ++s2) bestAttack = std::max(bestAttack, imp.q(s2, Intent::Attack));
        CHECK(g.hp_ < 100 && imp.experience() > 5 && bestAttack > 0,
              "el Rastrero aprende jugando: %d decisiones, atacar vale %.2f cuando acierta", imp.experience(), bestAttack);
        CHECK(g.bossBrain().experience() == 0, "el jefe tiene su propio cerebro (aun sin estrenar)");
    }
    {  // Viaje por la luz: apuntar a una farola y llegar hasta ella hecho luz
        Game g = fresh();
        place(g, {0, 0, -12});
        Vec3 lamp = g.lamps_[1];  // farola en (5,5, 3,35, -5,5)
        Controls aim;
        aim.aimOrigin = {0, 2.5f, -16};
        aim.aimDir = normalize(lamp - aim.aimOrigin);
        g.update(dt, aim);
        CHECK(g.lightTarget() >= 0 && length(g.lightTargetPos() - lamp) < 0.01f, "mirando a la farola, queda senalada");
        float l0 = g.mp_;
        Controls go = aim;
        go.beam = true;
        g.update(dt, go);
        CHECK(g.travelingByLight() && g.mp_ == l0 - 10, "Q: viaja hecho luz (cuesta 10 fuera de la luz)");
        run(g, 1.5f, aim);
        float d = length(flat(g.hero_.body.pos - lamp));
        CHECK(!g.travelingByLight() && d < 1.0f && g.hero_.body.grounded, "llega junto a la farola (a %.2f m)", d);
        // Hasta lo alto de una torre del puente.
        Game p = fresh();
        p.loadLevel(LevelId::Puente);
        place(p, {0, 0.8f, 32});
        Vec3 top{5.4f, 8.9f, 28.5f};
        Controls up;
        up.aimOrigin = {-3, 3, 36};
        up.aimDir = normalize(top - up.aimOrigin);
        p.update(dt, up);
        Controls upGo = up;
        upGo.beam = true;
        p.update(dt, upGo);
        run(p, 2.0f);
        CHECK(p.hero_.body.pos.y > 8.0f && p.hero_.body.grounded, "de luz en luz hasta lo alto de una torre (y=%.1f)", p.hero_.body.pos.y);
        // Lo que tapa la luz impide viajar.
        Game q = fresh();
        place(q, {0, 0, -3.6f});
        Controls hidden;
        hidden.aimOrigin = {0, 2.0f, -7};
        hidden.aimDir = normalize(Vec3{0, 3.0f, 0} - hidden.aimOrigin);  // el prisma de la fuente, sobre la columna
        q.update(dt, hidden);
        CHECK(q.lightTarget() < 0 || length(q.lightTargetPos() - Vec3{0, 3.0f, 0}) < 0.01f, "solo se senalan luces a la vista");
        // En la luz, los Grises se debilitan y la Luz se recarga sola.
        Game w = fresh();
        w.nodes_[0].lit = true;
        w.nodes_[0].litTime = 10;
        addEnemy(w, EnemyKind::Shroom, w.nodes_[0].pos + Vec3{1, 0, 0});
        addEnemy(w, EnemyKind::Shroom, {0, 0, 10});
        w.hitEnemy(w.enemies_[0], {1, 0, 0}, 10, 0, 0, false);
        w.hitEnemy(w.enemies_[1], {1, 0, 0}, 10, 0, 0, false);
        CHECK(w.enemies_[0].health == 45 && w.enemies_[1].health == 50, "en la luz el golpe hace un 50%% mas");
        place(w, w.nodes_[0].pos + Vec3{2, 0, 0});
        w.mp_ = 0;
        w.enemies_.clear();
        run(w, 2.0f);
        CHECK(w.mp_ > 3, "en la luz la Luz se recarga sola (%.1f en 2 s)", w.mp_);
    }
    {  // Corte cargado: mantener el clic carga; al soltar, la onda atraviesa a varios
        Game g = fresh();
        place(g, {0, 0, -12});
        addEnemy(g, EnemyKind::Shroom, {0, 0, -7});
        addEnemy(g, EnemyKind::Shroom, {0, 0, -3});
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
        CHECK(g.enemies_[0].health < 60 && g.enemies_[1].health < 60, "la onda alcanza a los dos Domos (%.0f, %.0f)",
              g.enemies_[0].health, g.enemies_[1].health);
    }
    {  // Transformaciones y compañeros
        Game g = fresh();
        place(g, {0, 0, -10});
        float speed0 = g.hero_.attackSpeed;
        g.update(dt, press(&Controls::transform));
        CHECK(g.form() == Form::None, "sin nucleo no hay transformacion");
        g.inventory().add(ItemId::CoreAurora);
        g.update(dt, press(&Controls::transform));
        CHECK(g.form() == Form::Aurora && g.hero_.attackSpeed > speed0 * 1.4f && g.inventory().count(ItemId::CoreAurora) == 0,
              "Forma Aurora: gasta el nucleo y golpea mas rapido");
        run(g, 26.0f);
        CHECK(g.form() == Form::None && std::fabs(g.hero_.attackSpeed - speed0) < 1e-4f, "la forma dura 25 s");
        g.inventory().add(ItemId::CoreEcho);
        g.update(dt, press(&Controls::transform));
        CHECK(g.form() == Form::Echo && g.hero_.gravityScale < 0.5f && g.hero_.infiniteJumps, "Forma Eco: poca gravedad y saltos sin limite");

        Game h = fresh();
        place(h, {0, 0, -10});
        addEnemy(h, EnemyKind::Shroom, {0, 0, -5});
        h.mp_ = 30;
        h.update(dt, press(&Controls::summon));
        CHECK(h.companions_.empty(), "la chispa necesita 40 de Luz");
        h.mp_ = 50;
        h.update(dt, press(&Controls::summon));
        CHECK(h.companions_.size() == 1 && h.mp_ == 10, "chispa aliada invocada (cuesta 40)");
        run(h, 4.0f);
        CHECK(h.enemies_[0].health < 60, "la chispa dispara al Domo (vida %.0f)", h.enemies_[0].health);
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
        addEnemy(g, EnemyKind::Imp, {6, 0, -10});
        addEnemy(g, EnemyKind::Imp, {0, 0, -4});
        Controls c = press(&Controls::lockOn);
        c.camForward = {0, 0, 1};
        g.update(dt, c);
        CHECK(g.lock_ == 1, "fija al enemigo de delante de la camara (%d)", g.lock_);
        g.update(dt, c);
        CHECK(g.lock_ == 0, "otra vez: pasa al siguiente (%d)", g.lock_);
        g.update(dt, press(&Controls::unlock));
        CHECK(g.lock_ == -1, "soltar objetivo");
    }
    {  // Polilla: su orbe se devuelve de un golpe y la daña
        Game g = fresh();
        place(g, {0, 0, -10});
        Enemy& m = addEnemy(g, EnemyKind::Moth, {0, 0, -4}, AiState::Chase);
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
            g.enemies_.push_back(makeEnemy(EnemyKind::Imp, {0, 0, 5}, 0, 0));
            g.killEnemy(g.enemies_.back());
        }
        CHECK(std::fabs(g.croma_ - (c0 + 3 * 0.015f)) < 1e-4f && g.splashes_.size() == 3,
              "3 Grises disipados: croma %.1f%%, %zu salpicaduras", g.croma_ * 100, g.splashes_.size());
        run(g, 1.0f);
        g.enemies_.clear();
        g.enemies_.push_back(makeEnemy(EnemyKind::Imp, g.nodes_[0].pos + Vec3{3, 0, 0}, 0, 0));
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
            g.enemies_.push_back(makeEnemy(EnemyKind::Imp, {0, 0, 5}, 0, 0));
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
        CHECK(g.level() == LevelId::Plaza && g.partIndex() == 0 && g.enemies_.size() == 2,
              "plaza, parte 1: el combate del despertar empieza (%zu Grises)", g.enemies_.size());
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
        CHECK(g.partIndex() == 2 && g.gates_[0].closed && !g.receptors_[0].lit,
              "tres nodos: parte 3, la puerta sigue sellada (rompecabezas de luz)");
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
        CHECK(g.level() == LevelId::Mercado && g.levelName() == "Mercado de los Ecos" && std::fabs(g.croma_ - 0.05f) < 1e-3f,
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
        bool boss = g.enemies_.size() == 1 && g.enemies_[0].boss && g.enemies_[0].size > 2.0f;
        CHECK(boss, "tras examinar el puesto aparece «%s»", g.enemies_.empty() ? "-" : g.enemies_[0].name());
    }
    {  // Puente: sin suelo, caer devuelve al último farol encendido
        Game g;
        g.init(GAME_ASSET_DIR);
        skipDialogues(g);
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
    {  // Historia en el juego: la intro detiene todo hasta leerla
        Game g;
        g.init(GAME_ASSET_DIR);
        CHECK(g.story_ && g.hero_.lying == 1, "con guion: el Prismero despierta tumbado");
        run(g, 3.0f);
        CHECK(g.dialogueBlocking() && g.enemies_.empty(), "la intro bloquea: sin Grises todavia");
        CHECK(g.speakerName("brizna") == "???", "Brizna aun no tiene nombre");
        Controls adv;
        adv.advance = true;
        for (int i = 0; i < 200 && g.dialogueBlocking(); ++i) {
            g.update(dt, adv);
            run(g, 0.05f);
        }
        CHECK(!g.dialogueBlocking() && g.speakerName("brizna") == "Brizna", "tras leer la intro, Brizna tiene nombre");
        run(g, 1.0f);
        CHECK(!g.enemies_.empty() && g.hero_.lying == 0, "tras la intro empieza el primer combate y el Prismero esta de pie");
        // Eco de memoria: aparece con su orden y se recoge al pasar.
        g.storyCommand({true, "eco_activa", "1"});
        g.hero_.body.pos = g.echoes_[0].pos - Vec3{0, 0.9f, 0};
        g.update(dt, {});
        g.update(dt, {});
        CHECK(g.echoes_[0].taken && g.played_.count(g.echoes_[0].scene), "el eco de memoria lanza su recuerdo");
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
    {  // Derrota y vuelta a empezar
        Game g = fresh();
        g.hurtHero({0, 0, 1}, 500, 2, nullptr);
        CHECK(g.hero_.downTimer >= 0, "derrotado con 0 PV");
        run(g, 3.2f);
        CHECK(g.hero_.downTimer < 0 && g.hp_ == 100 && length(g.hero_.body.pos - g.heroStart_) < 0.1f,
              "reaparece en el inicio con la vida llena");
    }
    std::printf("\n%s (%d fallos)\n", fails ? "HAY FALLOS" : "TODO OK", fails);
    return fails ? 1 : 0;
}
