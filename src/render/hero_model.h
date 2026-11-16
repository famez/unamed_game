#pragma once

// Protagonista cargada de un .glb (Vela en la luz, Hollín en la sombra). El modelo tiene que
// seguir la jerarquía de la ficha (assets/models/FICHA_VELA_HOLLIN.md): grupos con nombre de
// hueso (Hips > Spine > Neck > Head, Shoulder_L > UpperArm_L > LowerArm_L > Hand_L,
// UpperLeg_L > LowerLeg_L > Foot_L...) con el pivote en la articulación, en pose A.
//
// Cada malla se pega al hueso del juego que corresponde a su grupo; el torso y el faldón del
// abrigo se reparten entre varios huesos para doblarse con la espalda y las piernas. Lo que
// se mueve por inercia (coleta o melena, ahoge, colas de la bufanda, farol) va en piezas
// aparte con su pivote, y el arma (Prop_Sword / Prop_Hammer) en la suya, con el agarre en el
// origen y la hoja hacia +Y.

#include "render/models.h"

#include <string>
#include <vector>

enum class HeroPartKind { Hair, Ahoge, ScarfFront, ScarfBack, Lantern };

struct HeroPart {
    HeroPartKind kind;
    int mesh = -1;
    int joint = 0;                        // hueso del que cuelga
    Mat4 inverseBind = Mat4::identity();  // de ese hueso
    Vec3 pivot;                           // articulación de la pieza, en espacio del hueso
    Vec3 glow;                            // centro de lo que brilla (el farol), en espacio del hueso
    bool hasGlow = false;
};

struct HeroGlbModel {
    bool loaded = false;
    BuiltModel body;    // con contorno
    BuiltModel detail;  // sin contorno: cara y lo que brilla
    std::vector<HeroPart> parts;
    int prop = -1;          // arma (malla con un solo hueso)
    float propLength = 0;   // del agarre a la punta (m)
    uint32_t glowColor = 0; // color de lo que brilla (para la luz del farol)
};

// Carga y sube un modelo. Falso (y el motivo en `error`) si falta algo.
bool loadHeroGlb(Renderer& r, const std::string& path, HeroGlbModel& out, std::string& error);
// Carga assets/models/vela.glb y hollin.glb (si no están, se usa el modelo de siempre).
void loadHeroModels(Renderer& r, const std::string& assetDir);
// Lo dibuja con la postura `j` (matrices mundo de los huesos): el cuerpo con contorno `line`,
// la cara y lo que brilla sin él, y las piezas con inercia. `lag`: retraso del muelle del pelo
// en el espacio de la cabeza; `run`: 0..1 de la carrera; `life`: 0..1, el brillo del farol.
void drawHeroGlb(Renderer& r, const HeroGlbModel& m, const Mat4* j, Vec3 lag, float run, float time, float flash,
                 uint32_t line, float life);
// El modelo de ese aspecto, o nullptr si no se cargó.
const HeroGlbModel* heroGlb(HeroLook look);
