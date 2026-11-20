#pragma once

// Animaciones de la protagonista hechas a mano fuera del juego (assets/models/
// vela_animaciones.glb: correr, caminar, saltar, combo de tres golpes y voltereta). El .glb
// trae, por fotograma, el giro local de cada grupo de la ficha (Hips, Spine, UpperArm_L...) y
// la posición de la cadera; al cargarlo se pasa al esqueleto del juego (Euler Y·X·Z por hueso)
// a 60 fotogramas por segundo, así se mezcla con la animación procedural como cualquier pose.
// Sin el fichero, el personaje se anima como siempre.
//
// Hollín tiene los suyos (hollin_animaciones.glb: correr, caminar y tres golpes de martillo).
// Allí lleva el martillo en la mano izquierda; el juego pone el arma en la derecha, así que se
// leen en espejo (izquierda por derecha). Sin el fichero, Hollín usa los de Vela.

#include "actors/skeleton.h"

#include <string>

enum class HeroClip {
    Run, Walk, Jump, Combo, Roll,                      // Vela (vela_animaciones.glb)
    HollinRun, HollinWalk, HammerCombo, HammerLeap, HammerWhirl,  // Hollín (hollin_animaciones.glb)
    Count
};

struct ClipSample {
    Pose pose;       // giros de los huesos del juego; rootOffset: desplazamiento de la cadera
    float spin = 0;  // voltereta: vuelta de la cadera en X (rad, sin envolver), ya quitada de la pose
};

// Lee los clips. Falso (y el motivo en `error`) si falta el fichero o alguno de los clips.
bool loadHeroClips(const std::string& path, std::string& error);
bool loadHollinClips(const std::string& path, std::string& error);
bool heroClipsLoaded();
bool hollinClipsLoaded();
float heroClipDuration(HeroClip c);
// Pose del clip en el instante `t` (s); los ciclos y las acciones se repiten.
ClipSample sampleHeroClip(HeroClip c, float t);
