#pragma once

// Efectos de sonido. La lógica del juego solo emite eventos (qué y dónde); el audio los
// sintetiza al arrancar (synth.cpp) y los mezcla (audio.cpp).
enum class Sfx {
    Slash, Whip, Hammer, Hit, HitHeavy, Jump, Dodge, Guard, PerfectGuard, HeroHurt, EnemyWindup, EnemyDie,
    Pickup, Coin, ItemGet, Fire, FireBoom, Ice, Cure, Chest, Crate, WaveStart, Echo, Blip, MenuMove, MenuOk,
    Count
};

#include "vmath.h"

struct SoundEvent {
    Sfx id;
    Vec3 pos;
    bool positional = true;  // false: suena igual en los dos oídos (interfaz, diálogo)
    float gain = 1, pitch = 1;
};
