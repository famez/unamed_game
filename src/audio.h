#pragma once

// Salida de audio: PulseAudio (también PipeWire) cargado en tiempo de ejecución con
// dlopen, sin cabeceras ni dependencias al compilar; si no está, el juego va en silencio.
// Un hilo mezcla las capas de música y los efectos y los envía al servidor de sonido.

#include "sfx.h"
#include "synth.h"

#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

class Audio {
public:
    // Genera los efectos, carga la música de la caché o la sintetiza (en segundo plano) y
    // arranca la salida. `cacheDir`: dónde guardar los WAV de la música.
    bool init(const std::string& cacheDir);
    void shutdown();
    bool enabled() const { return running_; }

    // `pan` -1 (izquierda) .. 1 (derecha); `pitch` 1 = tono original.
    void play(Sfx id, float gain = 1, float pan = 0, float pitch = 1);
    // Volumen deseado de cada capa de la música (se alcanza con suavidad).
    void setStemGains(const float gains[int(Stem::Count)]);
    void setMasterVolume(float v) { master_ = v; }

private:
    struct Voice {
        const StereoBuffer* buf;
        double pos;
        float gain, pan, pitch;
    };
    void run();
    void mix(int16_t* out, int frames);

    void* lib_ = nullptr;
    void* stream_ = nullptr;
    std::thread thread_, loader_;
    std::atomic<bool> running_{false};
    std::atomic<bool> musicReady_{false};
    std::mutex mutex_;
    std::vector<StereoBuffer> sfx_;
    Music music_;
    size_t musicPos_ = 0;
    float stemTarget_[int(Stem::Count)] = {};
    float stemGain_[int(Stem::Count)] = {};
    std::vector<Voice> voices_;
    std::atomic<float> master_{0.8f};
};
