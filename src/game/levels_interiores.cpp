// Interiores: zonas pequeñas a las que se entra por una puerta de otra (Game::goToZone). La
// sala es una sola pieza (tools/props/interiores.py) con la puerta de salida en +z; aquí, quien
// atiende, la clientela, la puerta y la llegada.
//   - Botequim (Largo de los Arcos): Seu Ademar detrás de la barra; se descansa.
//   - Taberna da Lapa (Praça do Coreto): Joca en la barra y la samba en el escenario del fondo.

#include "game/game.h"

#include <cmath>

namespace {

// En una sala con techo, a la altura del suelo de dentro (no encima del tejado).
float floorUnder(const World& w, Vec3 p) {
    RayHit h;
    Vec3 a = p + Vec3{0, 1.2f, 0}, b = p - Vec3{0, 2, 0};
    if (segmentVsWorld(a, b, w, h)) return a.y + (b.y - a.y) * h.t;
    return 0;
}

}  // namespace

void Game::buildBotequim() {
    levelName_ = "Botequim do Ademar";
    world_.ground = true;
    floorColor_ = rgba(90, 60, 40);
    night_ = 0.55f;
    darkScale_ = 0.35f;
    fogScale_ = 0.2f;
    viewDistance_ = 60;
    addProp("botequim", {0, 0, 0}, 0, 1, false);
    rebuildSolids();
    auto person = [&](const char* model, Vec3 p, float yaw, float wander, const std::string& name = "", NpcRole role = NpcRole::Townsfolk,
                      std::vector<std::string> lines = {}) {
        addNpc(model, p, wander, name, role, std::move(lines));
        Npc& n = npcs_.back();
        p.y = floorUnder(world_, p);
        n.home = n.target = p;
        n.body.teleport(p, yaw);
    };
    person("cocinero", {-0.6f, 0, -3.05f}, 0, 0.3f, "Seu Ademar", NpcRole::Inn,
           {"Siéntate. La cerveza está tibia, pero es cerveza.", "Aquí el atardecer dura lo que dura la conversación. O sea, siempre.",
            "Duerme en el banco del fondo. Nadie te va a despertar."});
    person("pregonero", {-2.6f, 0, -1.3f}, 2.6f, 0.4f);  // acodado en la barra
    person("abuelo", {-3.2f, 0, 1.5f}, 0.5f, 0.3f, "Seu Lauro", NpcRole::Townsfolk,
           {"Antes el bonde pasaba cada diez minutos. Ahora pasa el mismo minuto, y no se va.",
            "Juego al dominó solo. Gano siempre, pero no me alegra."});
    person("vendedora", {1.9f, 0, 1.6f}, -2.4f, 0.6f);
    doors_.push_back({{0, 0, 3.4f}, LevelId::Rio, arrival::kRioBotequim, "Salir · al Largo de los Arcos"});
    arrivals_.push_back({{0, 0, 2.6f}, kPi});
    heroStart_ = {0, 0, 2.6f};
    heroStartYaw_ = kPi;
}

void Game::buildTabernaLapa() {
    levelName_ = "Taberna da Lapa";
    world_.ground = true;
    floorColor_ = rgba(90, 60, 40);
    night_ = 0.6f;
    darkScale_ = 0.35f;
    fogScale_ = 0.2f;
    viewDistance_ = 60;
    addProp("taberna_lapa_dentro", {0, 0, 0}, 0, 1, false);
    rebuildSolids();
    auto person = [&](const char* model, Vec3 p, float yaw, float wander, const std::string& name = "", NpcRole role = NpcRole::Townsfolk,
                      std::vector<std::string> lines = {}) {
        addNpc(model, p, wander, name, role, std::move(lines));
        Npc& n = npcs_.back();
        p.y = floorUnder(world_, p);
        n.home = n.target = p;
        n.body.teleport(p, yaw);
    };
    person("cafetero", {-4.85f, 0, -0.6f}, kPi * 0.5f, 0.3f, "Joca", NpcRole::Inn,
           {"Aquí se toca samba hasta que se haga de noche. Llevamos mucho tocando.",
            "Quédate. El que se va de la Lapa siempre vuelve, aunque sea a por el sombrero."});
    person("musica", {3.35f, 0, -3.65f}, 0.2f, 0.2f, "Dona Zica", NpcRole::Townsfolk,
           {"La samba no necesita que pase el tiempo. Lo marca ella.", "Este pandeiro era de mi madre. Suena igual que entonces."});
    person("trapera", {0.2f, 0, 1.4f}, -0.8f, 0.6f);
    person("oficinista", {-2.0f, 0, -0.9f}, 1.8f, 0.3f);
    person("barquero", {2.9f, 0, 1.2f}, 2.4f, 0.5f, "Tião", NpcRole::Townsfolk,
           {"Salgo con la barca y el mar no se mueve. Remo igual, por costumbre.",
            "Arriba, en el Pan de Azúcar, dicen que se ve la hora que era. Yo no he subido."});
    doors_.push_back({{0, 0, 4.15f}, LevelId::Rio, arrival::kRioLapa, "Salir · a la Praça do Coreto"});
    arrivals_.push_back({{0, 0, 3.3f}, kPi});
    heroStart_ = {0, 0, 3.3f};
    heroStartYaw_ = kPi;
}
