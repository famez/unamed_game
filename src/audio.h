#pragma once

// Salida de audio: PulseAudio (también PipeWire) cargado en tiempo de ejecución con
// dlopen, sin cabeceras ni dependencias al compilar; si no está, el juego va en silencio.
// Un hilo mezcla las capas de música y los efectos y los envía al servidor de sonido.

#include "sfx.h"
#include "synth.h"

#include <atomic>
#include <condition_variable>
#include <map>
#include <memory>
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
    // Pista que debe sonar (cambia con un fundido; si no está lista, se prepara antes).
    void playTrack(TrackId id);
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
    void loaderLoop();
    void mix(int16_t* out, int frames);
    void mixTrack(Music& m, float* acc, int frames, const float* g0, const float* g1);

    void* lib_ = nullptr;
    void* stream_ = nullptr;
    std::thread thread_, loader_;
    std::atomic<bool> running_{false};
    std::mutex mutex_;
    std::condition_variable loaderCv_;
    std::vector<StereoBuffer> sfx_;
    std::string cacheDir_;
    std::map<int, std::unique_ptr<Music>> tracks_;  // pistas ya preparadas
    int wanted_ = -1, current_ = -1, previous_ = -1;
    float fade_ = 1;  // 0..1: entrada de la pista actual (y salida de la anterior)
    float stemTarget_[int(Stem::Count)] = {};
    float stemGain_[int(Stem::Count)] = {};
    std::vector<Voice> voices_;
    std::atomic<float> master_{0.8f};
};
