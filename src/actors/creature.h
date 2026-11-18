#pragma once

// Criaturas del bestiario (assets/models/bestiario, ver su FICHA_BESTIARIO.md): animales y
// gentes de las farolas que no pelean. Cada una trae su propio esqueleto en el .glb
// (cuadrúpedo, ave posada, caracol, cetáceo, un gigante...): aquí se lee de la piel del
// fichero (nombres, jerarquía y pose de reposo, sin rotaciones) y se anima por rotaciones
// locales de sus huesos, buscados por nombre. El cangrejo de cristal, que sí pelea, es un
// enemigo más (actors/enemy.h).

#include "actors/skeleton.h"

#include <string>

struct GlbFile;

enum class CreatureKind { Mechita, Hollinete, Buho, Topo, Caracol, Larguero, Ballena, Count };

const char* creatureFile(CreatureKind k);  // nombre del .glb, sin la extensión
const char* creatureName(CreatureKind k);  // como se le llama en el juego
float creatureScale(CreatureKind k);       // escala de dibujo (la Mechita, algo mayor para que se lea)

// Esqueleto de una criatura: los huesos de la piel del .glb, padres antes que hijos. Los
// nombres de `joints` apuntan a `names` (por eso no se copia).
struct CreatureRig {
    int count = 0;
    JointDef joints[kMaxJoints] = {};
    std::string names[kMaxJoints];

    CreatureRig() = default;
    CreatureRig(const CreatureRig&) = delete;
    CreatureRig& operator=(const CreatureRig&) = delete;
    bool loaded() const { return count > 0; }
    int find(const char* name) const;  // -1 si no lo tiene
};

// Saca el esqueleto de la (primera) piel del .glb. Falso si no tiene, o si pasa de kMaxJoints.
bool creatureRigFromGlb(const GlbFile& g, CreatureRig& out, std::string& error);
// Lee los esqueletos de las criaturas que estén en la carpeta; devuelve cuántos.
int loadCreatureRigs(const std::string& dir);
const CreatureRig& creatureRig(CreatureKind k);  // vacío (count 0) si no está su .glb

// Lo que mueve a una criatura en un instante (lo decide el juego, game/creatures.cpp).
struct CreatureMotion {
    float time = 0;      // reloj (con el desfase propio de cada una)
    float phase = 0;     // ciclo de pasos o de aleteo
    float speed = 0;     // lo que anda (m/s); 0: quieta
    float look = 0;      // giro de la cabeza respecto al cuerpo (rad)
    float hide = 0;      // la Mechita apagándose, el caracol en su concha, el topo atento (0..1)
    float hop = -1;      // salto o vuelo en curso, de 0 a 1 (< 0: no)
    float lighting = -1; // el Larguero: segundos encendiendo una farola (< 0: no)
};
// Pose de una criatura (rotaciones locales de sus huesos, por nombre) según lo que hace.
void poseCreature(CreatureKind kind, const CreatureRig& rig, const CreatureMotion& m, Pose& out);
