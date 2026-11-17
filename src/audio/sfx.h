#pragma once

// Efectos de sonido. La lógica del juego solo emite eventos (qué y dónde); el audio los
// sintetiza al arrancar (synth.cpp) y los mezcla (audio.cpp).
enum class Sfx {
    Slash, Whip, Hammer, Hit, HitHeavy, Jump, Dodge, Guard, PerfectGuard, HeroHurt, EnemyWindup, EnemyDie,
    Pickup, Coin, ItemGet, Fire, FireBoom, Ice, Cure, Chest, Crate, WaveStart, Echo, Blip, MenuMove, MenuOk,
    // Voz de la protagonista (síntesis por formantes). Vela (luz): aguda, limpia, inocente;
    // Hollín (sombra): grave, ronca, burlona.
    VelaAttack1, VelaAttack2, VelaFinisher, VelaJump, VelaHurt, VelaDodge,
    HollinAttack1, HollinAttack2, HollinFinisher, HollinJump, HollinHurt, HollinDodge, HollinLaugh,
    // Los barks del Abuelo Candil (audio/barks.h): una toma por frase, que elige el juego.
    Abuelo,
    Count
};

#include "core/vmath.h"

struct SoundEvent {
    Sfx id;
    Vec3 pos;
    bool positional = true;  // false: suena igual en los dos oídos (interfaz, diálogo)
    float gain = 1, pitch = 1;
    int take = -1;  // la toma concreta (-1: una al azar)
};
