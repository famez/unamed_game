#pragma once

// Síntesis de audio por código: la música (capas que se mezclan según el juego) y los
// efectos de sonido. Todo se genera al arrancar (y la música se guarda en caché como WAV).
// Lógica pura: sin dependencias de sistema; la usan el audio y los tests.

#include "audio/sfx.h"

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
    size_t pos = 0;  // posición de reproducción (la usa el mezclador)
};

// Una pista por escenario y la del jefe. Cada una en bucle sin costura (las colas de la
// reverberación pasan del final al principio). Farolas no se sintetiza: es el tema de Vela y
// Hollín (assets/audio/musica), dos capas grabadas (luz en Pad, sombra en Melody) que se cruzan
// al cambiar de forma.
enum class TrackId { Plaza, Mercado, Puente, Jefe, Farolas, Count };
Music renderMusic(TrackId id);
StereoBuffer renderSfx(Sfx id);

bool saveWav(const std::string& path, const StereoBuffer& buf);
bool loadWav(const std::string& path, StereoBuffer& buf);
// Cualquier WAV (PCM de 16, 24 o 32 bits, o coma flotante; mono o estéreo; cualquier
// frecuencia), convertido a estéreo a kSampleRate.
bool loadAnyWav(const std::string& path, StereoBuffer& buf);
// Un .ogg (Vorbis) con libvorbisfile, cargada en tiempo de ejecución (sin ella, falso).
bool loadOgg(const std::string& path, StereoBuffer& buf);

// Las tomas de un efecto. Las voces de la protagonista se buscan grabadas en `voiceDir`
// (vela_golpe_1.wav, vela_golpe_2.wav, hollin_dolor_1.wav...; ver assets/audio/voces/LEEME.md):
// si un personaje tiene grabaciones, todas sus voces salen de ellas (lo que falte se cubre con
// la más parecida o se calla); si no tiene ninguna, se sintetizan. El resto, sintetizado.
std::vector<StereoBuffer> sfxVariants(Sfx id, const std::string& voiceDir);
