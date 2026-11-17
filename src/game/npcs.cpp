// Habitantes de los mapas grandes: pasean por su zona a paso tranquilo (con el mismo
// Character que la protagonista, así andan y se paran con sus animaciones), descansan un
// rato y, si la protagonista se les acerca, se paran y la miran. Los lejanos no se mueven
// (no se ven) para no gastar.

#include "game/game.h"

#include <algorithm>
#include <cmath>

namespace {

constexpr float kActiveRange = 90.0f;  // más lejos, quietos
constexpr float kWalk = 0.3f;          // fracción de la carrera: paseo
constexpr float kLook = 3.5f;          // a esta distancia se paran a mirarla

float rnd(uint32_t& s) {
    s = s * 1664525u + 1013904223u;
    return float(s >> 8) / 16777216.0f;
}

}  // namespace

void Game::addNpc(const std::string& model, Vec3 pos, float radius, const std::string& name, NpcRole role,
                  std::vector<std::string> lines) {
    Npc n;
    n.model = model;
    n.name = name;
    n.role = role;
    n.lines = std::move(lines);
    n.seed = uint32_t(npcs_.size()) * 2654435761u + 17u;
    pos.y = groundHeight(pos + Vec3{0, 30, 0});
    n.home = n.target = pos;
    n.radius = radius;
    n.body.teleport(pos, rnd(n.seed) * 2 * kPi);
    n.body.setArmed(false, true);
    n.wait = rnd(n.seed) * 4.0f;
    npcs_.push_back(n);
}

void Game::updateNpcs(float dt) {
    npcLineTime_ = std::max(0.0f, npcLineTime_ - dt);
    for (Npc& n : npcs_) {
        Vec3 toHero = flat(hero_.body.pos - n.body.body.pos);
        float dh = length(toHero);
        if (dh > kActiveRange) continue;
        Vec3 dir{};
        // Pabilo sigue a Vela mientras ella esté en su zona y no haya Grises cerca; si no, vuelve.
        bool follow = false;
        if (n.role == NpcRole::Follower) {
            bool danger = false;
            for (const Enemy& e : enemies_) danger |= e.alive() && length(flat(e.body.pos - n.body.body.pos)) < 16.0f;
            follow = !danger && length(flat(hero_.body.pos - n.home)) < n.radius;
            n.target = follow ? hero_.body.pos - hero_.forward() * 2.0f : n.home;
        }
        float stillTarget = 0;
        if (n.role == NpcRole::Follower && (follow || length(flat(n.target - n.body.body.pos)) > 1.0f) && dh > 2.6f) {
            Vec3 d = flat(n.target - n.body.body.pos);
            float far = length(d);
            if (far > 0.8f) dir = normalize(d) * clampf(far / 6.0f, kWalk, 1.0f);  // se da prisa si se queda atrás
            if (far > 25.0f) n.body.teleport(n.target, n.body.yaw);
            stillTarget = far > 0.8f ? 0.0f : 1.0f;
        } else if (dh < kLook && hero_.downTimer < 0) {  // se para y la mira
            n.body.yaw += std::remainder(std::atan2(toHero.x, toHero.z) - n.body.yaw, 2 * kPi) * std::min(1.0f, dt * 4.0f);
            n.wait = std::max(n.wait, 1.0f);
            stillTarget = 1;
        } else if (n.wait > 0) {
            n.wait -= dt;
            stillTarget = 1;
            if (n.wait <= 0) {  // otro destino dentro de su zona
                float a = rnd(n.seed) * 2 * kPi, r = std::sqrt(rnd(n.seed)) * n.radius;
                n.target = n.home + Vec3{std::cos(a) * r, 0, std::sin(a) * r};
                n.walked = 0;
            }
        } else {
            Vec3 d = flat(n.target - n.body.body.pos);
            n.walked += dt;
            if (length(d) < 0.7f || n.walked > 12.0f) {  // llegó (o algo le corta el paso)
                n.wait = 2.0f + rnd(n.seed) * 5.0f;
            } else {
                dir = normalize(d) * kWalk;
            }
        }
        n.still = moveTowards(n.still, stillTarget, dt * 2.5f);
        n.body.update(dt, dir, false, world_, n.body.yaw);
    }
}

// --- Vecinos con nombre -------------------------------------------------------------------

namespace {

constexpr float kLineTime = 5.0f;  // lo que dura en pantalla lo que dicen

// Los consejos de Brasa (uno cada vez que se le habla).
const char* const kBrasaTips[] = {
    "Tres golpes y el cuarto, el remate. No te pares a mirar.",
    "Corre y golpea: embestida. Con la guardia alta y golpe, lo levantas.",
    "Bloquea justo antes de que te dé. Entonces devuélveselo todo.",
    "En el aire, guardia y golpe: cae como una farola.",
    "Esquiva y golpea en el mismo aliento. Eso es un contraataque.",
};

}  // namespace

void Game::talkTo(Npc& n) {
    n.wait = std::max(n.wait, 3.0f);
    std::string text = n.lines.empty() ? std::string() : n.lines[size_t(n.line) % n.lines.size()];
    ++n.line;
    switch (n.role) {
        case NpcRole::Inn:  // descansa: vida al máximo y punto de control
            hp_ = maxHp_;
            checkpoint_ = hero_.body.pos;
            checkpointYaw_ = hero_.yaw;
            notify("Descansas en la fonda · vida llena y punto de control", rgba(255, 210, 140));
            uiSound(Sfx::ItemGet, 0.6f, 0.9f);
            break;
        case NpcRole::Shop: shopRequest_ = true; break;
        case NpcRole::Trainer: text = kBrasaTips[size_t(n.line - 1) % std::size(kBrasaTips)]; break;
        default: break;
    }
    if (!text.empty()) {
        npcSpeaker_ = n.name;
        npcLine_ = text;
        npcLineTime_ = kLineTime;
    }
}

void Game::placeCitizens() {
    // Busca suelo libre cerca del punto (y que haya suelo debajo: los puentes van sobre el vacío).
    auto add = [&](const char* model, Vec3 p, const char* name, NpcRole role, std::vector<std::string> lines, float radius = 4) {
        if (!devFreeSpot(p, 0.7f)) return;
        RayHit under;
        Vec3 top = p + Vec3{0, 3, 0};
        if (!segmentVsWorld(top, p - Vec3{0, 4, 0}, world_, under)) return;
        addNpc(model, p, radius, name, role, std::move(lines));
    };
    switch (level_) {
        case LevelId::Plaza:  // la plaza: la fonda, el taller, la entrenadora y los vecinos
            add("senora_mecha", {-11, 0, -12}, "Señora Mecha", NpcRole::Inn,
                {"Siéntate. Hay sopa, y el fogón de la farola aún tira.", "Duerme un rato. Yo vigilo la puerta."}, 2);
            add("cafetero", {-8, 0, -13.5f}, "Mozo de la fonda", NpcRole::Townsfolk,
                {"¿Café? Lo hago con el vapor de la farola. Sabe a cobre.", "La Señora Mecha no cierra nunca. Ni ahora."}, 3);
            add("umbra", {11, 0, -12}, "Doctora Umbra", NpcRole::Shop,
                {"Piezas de luz, piezas de sombra. No preguntes de dónde salen.",
                 "Esta lente mira la sombra. A veces la sombra me mira a mí."}, 2);
            add("chica_neon", {13.5f, 0, -10}, "Chispa", NpcRole::Townsfolk,
                {"La doctora dice que no toque el cristal violeta. …Lo he tocado.", "Si se te rompe algo, tráemelo. Casi siempre lo arreglo."}, 3);
            add("brasa", {-12, 0, 9}, "Brasa", NpcRole::Trainer, {}, 2);
            add("pabilo", {-3, 0, -6}, "Pabilo", NpcRole::Follower,
                {"¿Me dejas encender la próxima? Solo una.", "Llevo la escalera. Por si acaso.",
                 "Brasa dice que tú nunca te saltabas una farola."}, 24);
            add("abuelo", {6, 0, -10}, "Abuelo Candil", NpcRole::Townsfolk,
                {"Cuarenta años encendiéndolas. Ahora solo las cuento.", "Esa de ahí parpadea. Siempre parpadeó."}, 3);
            add("pregonero", {8, 0, 11}, "Pregonero", NpcRole::Townsfolk,
                {"¡Calle del Reloj, apagada! ¡Calle de las Cerillas, apagada!", "Cada noche tacho una calle más."}, 6);
            add("musica", {-7, 0, 12}, "Música callejera", NpcRole::Townsfolk,
                {"Los tubos son de una farola del puerto. Aún suenan a luz."}, 2);
            break;
        case LevelId::Mercado:  // los oficios de la luz
            add("aceitero", heroStart_ + Vec3{-4, 0, 6}, "Aceitero", NpcRole::Townsfolk, {"Aceite para farolas. Del bueno, del que brilla."}, 3);
            add("vidriera", heroStart_ + Vec3{4, 0, 10}, "Vidriera", NpcRole::Townsfolk,
                {"Este cristal no deja pasar la luz. Lo guardo para estudiarlo."}, 3);
            add("trapera", heroStart_ + Vec3{-3, 0, 16}, "Trapera", NpcRole::Townsfolk,
                {"Un brazo de latón, dos bombillas… Todo sirve para algo.", "¿Cristales de sombra? Yo no vendo eso. …¿Cuántos?"}, 4);
            add("relojero", heroStart_ + Vec3{3, 0, 22}, "Relojero de postes", NpcRole::Townsfolk,
                {"Las tengo puestas para encenderse a su hora. Pero ya no hay horas."}, 3);
            break;
        case LevelId::Puente:
            add("guardia_apagado", heroStart_ + Vec3{2.5f, 0, 2}, "Guardia apagado", NpcRole::Townsfolk,
                {"Todo en orden. Todo en orden. Todo… en orden."}, 1);
            add("monja_fanal", heroStart_ + Vec3{-2.5f, 0, 3}, "Monja del fanal", NpcRole::Townsfolk,
                {"La capilla sigue encendida. Mientras yo respire."}, 1);
            break;
        case LevelId::Callejones:
            add("deshollinadora", heroStart_ + Vec3{2, 0, 5}, "Deshollinadora", NpcRole::Townsfolk,
                {"Por dentro, las farolas están llenas de sombra. Alguien tiene que sacarla."}, 3);
            add("nino_sombras", heroStart_ + Vec3{-2, 0, 8}, "Niño de las sombras", NpcRole::Townsfolk,
                {"¡No pises la mía! …La sombra, digo."}, 5);
            break;
        case LevelId::Cueva:
            add("barquero", heroStart_ + Vec3{3, 0, 4}, "Barquero", NpcRole::Townsfolk,
                {"Al otro lado no hay farolas. Por eso se cruza en silencio."}, 2);
            break;
        default: break;
    }
}
