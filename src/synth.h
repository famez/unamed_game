#pragma once

// Síntesis de audio por código: la música (capas que se mezclan según el juego) y los
// efectos de sonido. Todo se genera al arrancar (y la música se guarda en caché como WAV).
// Lógica pura: sin dependencias de sistema; la usan el audio y los tests.

#include "sfx.h"

#include <cstdint>
#include <string>
#include <vector>

constexpr int kSampleRate = 44100;

// Audio estéreo entrelazado (I, D, I, D...) en 16 bits.
struct StereoBuffer {
    std::vector<int16_t> data;
    size_t frames() const { return data.size() / 2; }
};

// Capas de la música. Suenan siempre a la vez (mismo tempo y duración) y el juego decide
// el volumen de cada una: así la música crece con el color y se tensa en el combate.
enum class Stem { Pad, Melody, Pulse, Shimmer, Count };

struct Music {
    StereoBuffer stems[int(Stem::Count)];
    float loopSeconds = 0;
};

// "La primera luz": Si menor / Re mayor, 84 pulsaciones por minuto, 16 compases en bucle
// sin costura (las colas de la reverberación pasan del final al principio).
Music renderMusic();
StereoBuffer renderSfx(Sfx id);

bool saveWav(const std::string& path, const StereoBuffer& buf);
bool loadWav(const std::string& path, StereoBuffer& buf);
