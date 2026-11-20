// Interiores: zonas pequeñas a las que se entra por una puerta de otra (Game::goToZone). La
// sala es una sola pieza (tools/props/interiores.py) con la puerta de salida en +z; aquí, quien
// atiende, la clientela, la puerta y la llegada.
//   - Botequim (Largo de los Arcos): Seu Ademar detrás de la barra; se descansa. Posada de dos
//     alturas: la chimenea con sus butacas, la estantería y los cuartos de la galería.
//   - Taberna da Lapa (Praça do Coreto): Joca en la barra y la samba en el escenario del fondo;
//     también con chimenea, estantería y galería con cuartos.
// Oscuras y cálidas: alumbran el fuego (Game::hearths_), las velas y los apliques.

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

// Lo común: la sala (una pieza), su gente (en el suelo de dentro: abajo o en la galería), la
// chimenea y la puerta de salida.
void Game::buildInterior(const char* piece, Vec3 hearth, const std::vector<InteriorPerson>& people) {
    world_.ground = true;
    floorColor_ = rgba(60, 40, 28);
    night_ = 0.68f;  // oscura: alumbran el fuego, las velas y los apliques
    darkScale_ = 0.35f;
    fogScale_ = 0.15f;
    viewDistance_ = 60;
    addProp(piece, {0, 0, 0}, 0, 1, false);
    rebuildSolids();
    hearths_ = {hearth};
    for (const InteriorPerson& ip : people) {
        addNpc(ip.model, ip.pos, ip.wander, ip.name, ip.role, ip.lines);
        Npc& n = npcs_.back();
        Vec3 p = ip.pos;
        p.y = floorUnder(world_, p);
        n.home = n.target = p;
        n.body.teleport(p, ip.yaw);
    }
}

void Game::buildBotequim() {
    levelName_ = "Botequim do Ademar";
    const float G = 3.2f;  // la galería de arriba
    buildInterior("botequim", {5.14f, 0.28f, 0.4f},
                  {{"cocinero", {-0.6f, 0, -4.05f}, 0, 0.3f, "Seu Ademar", NpcRole::Inn,
                    {"Siéntate. La cerveza está tibia, pero es cerveza.", "Aquí el atardecer dura lo que dura la conversación. O sea, siempre.",
                     "Arriba, el tres está libre. Nadie te va a despertar."}},
                   {"pregonero", {-2.6f, 0, -2.3f}, 2.6f, 0.4f},  // acodado en la barra
                   {"abuelo", {2.3f, 0, 2.2f}, 2.0f, 0.2f, "Seu Lauro", NpcRole::Townsfolk,
                    {"Antes el bonde pasaba cada diez minutos. Ahora pasa el mismo minuto, y no se va.",
                     "Juego al dominó solo. Gano siempre, pero no me alegra.", "Este fuego lo encendí yo. No sé cuándo."}},
                   {"aceitero", {2.4f, 0, -1.4f}, 1.0f, 0.2f},  // calentándose las manos
                   {"vendedora", {1.0f, 0, 1.7f}, -2.4f, 0.5f},
                   {"deshollinadora", {-2.1f, 0, -0.4f}, 0.9f, 0.3f},
                   {"relojero", {4.55f, 0, 3.5f}, kPi * 0.5f, 0.1f, "Seu Firmino", NpcRole::Townsfolk,
                    {"Busco un libro que no termine. Los de aquí ya los he leído todos dos veces.",
                     "Arreglo relojes. Ahora solo los limpio."}},
                   {"granjera", {1.9f, 0, 3.6f}, -2.2f, 0.3f},
                   {"monja_fanal", {0.9f, G, -3.15f}, 0, 0.0f, "Irmã Lúcia", NpcRole::Townsfolk,
                    {"Desde aquí arriba se ve quién entra. Nadie entra.", "El tres tiene la ventana a la caleta. Siempre la misma luz."}}});
    doors_.push_back({{0, 0, 4.4f}, LevelId::Rio, arrival::kRioBotequim, "Salir · al Largo de los Arcos"});
    arrivals_.push_back({{0, 0, 3.1f}, kPi});
    heroStart_ = {0, 0, 3.1f};
    heroStartYaw_ = kPi;
}

void Game::buildTabernaLapa() {
    levelName_ = "Taberna da Lapa";
    const float G = 3.4f;
    buildInterior("taberna_lapa_dentro", {6.14f, 0.28f, 1.0f},
                  {{"cafetero", {-5.85f, 0, -0.6f}, kPi * 0.5f, 0.3f, "Joca", NpcRole::Inn,
                    {"Aquí se toca samba hasta que se haga de noche. Llevamos mucho tocando.",
                     "Quédate. El que se va de la Lapa siempre vuelve, aunque sea a por el sombrero.",
                     "Hay camas arriba. El fuego sube por la pared y no hace falta manta."}},
                   {"musica", {3.85f, 0, -4.4f}, 0.2f, 0.2f, "Dona Zica", NpcRole::Townsfolk,
                    {"La samba no necesita que pase el tiempo. Lo marca ella.", "Este pandeiro era de mi madre. Suena igual que entonces."}},
                   {"surfista", {5.3f, 0, -4.3f}, -0.4f, 0.1f},  // el del cavaquinho
                   {"chica_neon", {1.9f, 0, -2.2f}, 2.6f, 0.6f},  // baila delante del escenario
                   {"trapera", {0.9f, 0, 1.6f}, -0.8f, 0.6f},
                   {"oficinista", {-3.5f, 0, 1.2f}, -kPi * 0.5f, 0.3f},  // en la barra, al otro extremo
                   {"barquero", {3.2f, 0, -1.0f}, 1.2f, 0.3f, "Tião", NpcRole::Townsfolk,
                    {"Salgo con la barca y el mar no se mueve. Remo igual, por costumbre.",
                     "Arriba, en el Pan de Azúcar, dicen que se ve la hora que era. Yo no he subido."}},
                   {"guardia_apagado", {3.7f, 0, 3.3f}, 2.2f, 0.2f, "Cabo Nogueira", NpcRole::Townsfolk,
                    {"Fuera de servicio. Desde que se paró la hora, siempre estoy fuera de servicio.",
                     "Al fuego no le hace falta guardia. Me siento igual."}},
                   {"recolectora", {-0.6f, 0, 3.4f}, -2.8f, 0.4f},
                   {"vidriera", {-4.5f, G, 1.6f}, kPi * 0.5f, 0.0f, "Dona Inês", NpcRole::Townsfolk,
                    {"Duermo en el uno. Bajo a oír la samba y vuelvo a subir. Así todo el día que no acaba.",
                     "Desde la baranda se ve el escenario mejor que desde abajo."}}});
    doors_.push_back({{0, 0, 5.15f}, LevelId::Rio, arrival::kRioLapa, "Salir · a la Praça do Coreto"});
    arrivals_.push_back({{0, 0, 3.7f}, kPi});
    heroStart_ = {0, 0, 3.7f};
    heroStartYaw_ = kPi;
}
