// Test de la lógica del juego, sin ventana ni OpenGL: se rellena `Controls` a mano, se
// simulan pasos fijos de 1/60 s (determinista) y se comprueba el estado interno.
// Se compila con AddressSanitizer + UBSan. Ejecutar: ctest --test-dir build  (o build/logic_test)
#define private public  // solo para el test: acceso al estado interno
#include "game.h"
#undef private
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
        c.jump = c.attack = c.dodge = c.interact = c.lockOn = c.unlock = c.runCommand = false;
        c.command = -1;
        c.commandScroll = 0;
    }
}
Controls press(bool Controls::*flag) {
    Controls c;
    c.*flag = true;
    return c;
}

// Partida sin oleadas automáticas: cada test coloca sus enemigos. `place` pone al
// jugador ahí y le deja posarse en el suelo.
Game fresh() {
    Game g;
    g.init();
    g.waveTimer_ = 1e9f;
    return g;
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
    {  // Magias: Fuego gasta PM y daña; Cura cura; sin PM no sale
        Game g = fresh();
        place(g, {0, 0, -10});
        addEnemy(g, EnemyKind::Shroom, {0, 0, -5});
        Controls c;
        c.command = 0;
        run(g, 1.5f, c);
        CHECK(g.mp_ < 50 - 8 + 2, "Fuego gasta 8 PM (PM %.1f)", g.mp_);
        CHECK(g.enemies_[0].health < 60 && g.enemies_[0].burn > 0, "y quema a la seta (vida %.0f)", g.enemies_[0].health);
        g.hp_ = 40;
        c.command = 2;
        run(g, 1.0f, c);
        CHECK(g.hp_ > 80, "Cura: de 40 a %.0f PV", g.hp_);
        g.mp_ = 5;
        size_t before = g.projectiles_.size();
        c.command = 1;
        run(g, 0.6f, c);
        CHECK(g.projectiles_.size() == before && g.mp_ >= 5, "sin PM no se lanza Hielo");
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
    {  // Oleadas
        Game g;
        g.init();
        run(g, 2.2f);
        CHECK(g.wave_ == 1 && g.enemies_.size() == 3, "oleada 1: %zu enemigos", g.enemies_.size());
        for (Enemy& e : g.enemies_) g.killEnemy(e);
        run(g, 5.0f);
        CHECK(g.wave_ == 2 && g.enemies_.size() == 3, "oleada 2 al limpiar la 1: %zu enemigos", g.enemies_.size());
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
