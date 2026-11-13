#include "audio.h"

#include <dlfcn.h>
#include <sys/stat.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace {

// Lo mínimo de la API "simple" de PulseAudio (pulse/simple.h), declarado a mano.
struct PaSampleSpec {
    int format;  // PA_SAMPLE_S16LE = 3
    uint32_t rate;
    uint8_t channels;
};
struct PaBufferAttr {
    uint32_t maxlength, tlength, prebuf, minreq, fragsize;
};
using PaNew = void* (*)(const char*, const char*, int, const char*, const char*, const PaSampleSpec*, const void*,
                        const PaBufferAttr*, int*);
using PaWrite = int (*)(void*, const void*, size_t, int*);
using PaFree = void (*)(void*);
PaWrite paWrite = nullptr;
PaFree paFree = nullptr;

constexpr int kBlock = 512;  // frames por envío (~12 ms)
constexpr int kMusicVersion = 2;  // cambiarlo regenera la caché si cambia la partitura

void makeDirs(const std::string& path) {
    for (size_t i = 1; i <= path.size(); ++i)
        if (i == path.size() || path[i] == '/') mkdir(path.substr(0, i).c_str(), 0755);
}

}  // namespace

bool Audio::init(const std::string& cacheDir) {
    lib_ = dlopen("libpulse-simple.so.0", RTLD_NOW);
    if (!lib_) {
        std::fprintf(stderr, "Audio: no se encontró libpulse-simple; el juego irá sin sonido\n");
        return false;
    }
    auto paNew = (PaNew)dlsym(lib_, "pa_simple_new");
    paWrite = (PaWrite)dlsym(lib_, "pa_simple_write");
    paFree = (PaFree)dlsym(lib_, "pa_simple_free");
    if (!paNew || !paWrite || !paFree) return false;
    PaSampleSpec spec{3, kSampleRate, 2};
    // Búfer de ~60 ms en el servidor: poca latencia sin cortes.
    uint32_t bytes = uint32_t(kSampleRate * 4 * 0.06);
    PaBufferAttr attr{uint32_t(-1), bytes, uint32_t(-1), uint32_t(-1), uint32_t(-1)};
    int err = 0;
    stream_ = paNew(nullptr, "unamed_game", 1 /* reproducción */, nullptr, "juego", &spec, nullptr, &attr, &err);
    if (!stream_) {
        std::fprintf(stderr, "Audio: no se pudo abrir la salida (error %d)\n", err);
        return false;
    }
    for (int i = 0; i < int(Sfx::Count); ++i) sfx_.push_back(renderSfx(Sfx(i)));
    voices_.reserve(64);
    running_ = true;
    thread_ = std::thread(&Audio::run, this);
    // La música: de la caché si ya se generó; si no, se sintetiza en segundo plano (tarda
    // un poco) y se guarda para la próxima vez.
    loader_ = std::thread([this, cacheDir] {
        Music m;
        bool cached = true;
        for (int s = 0; s < int(Stem::Count) && cached; ++s) {
            char name[64];
            std::snprintf(name, sizeof(name), "/musica_v%d_capa%d.wav", kMusicVersion, s);
            cached = loadWav(cacheDir + name, m.stems[s]);
        }
        if (!cached) {
            m = renderMusic();
            makeDirs(cacheDir);
            for (int s = 0; s < int(Stem::Count); ++s) {
                char name[64];
                std::snprintf(name, sizeof(name), "/musica_v%d_capa%d.wav", kMusicVersion, s);
                saveWav(cacheDir + name, m.stems[s]);
            }
        }
        std::lock_guard<std::mutex> lock(mutex_);
        music_ = std::move(m);
        musicPos_ = 0;
        musicReady_ = true;
    });
    return true;
}

void Audio::shutdown() {
    if (loader_.joinable()) loader_.join();
    running_ = false;
    if (thread_.joinable()) thread_.join();
    if (stream_ && paFree) paFree(stream_);
    stream_ = nullptr;
    if (lib_) dlclose(lib_);
    lib_ = nullptr;
}

void Audio::play(Sfx id, float gain, float pan, float pitch) {
    if (!running_ || int(id) >= int(sfx_.size()) || sfx_[size_t(id)].frames() < 2) return;
    std::lock_guard<std::mutex> lock(mutex_);
    if (voices_.size() >= 48) voices_.erase(voices_.begin());  // se corta la más antigua
    voices_.push_back({&sfx_[size_t(id)], 0.0, gain, clampf(pan, -1, 1), pitch});
}

void Audio::setStemGains(const float gains[int(Stem::Count)]) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (int i = 0; i < int(Stem::Count); ++i) stemTarget_[i] = gains[i];
}

void Audio::run() {
    int16_t block[kBlock * 2];
    while (running_) {
        mix(block, kBlock);
        int err = 0;
        if (paWrite(stream_, block, sizeof(block), &err) < 0) break;  // bloquea al ritmo del servidor
    }
}

void Audio::mix(int16_t* out, int frames) {
    std::vector<float> acc(size_t(frames) * 2, 0.0f);
    std::lock_guard<std::mutex> lock(mutex_);
    // Música: las capas a la vez, cada una con su volumen (rampa suave por bloque).
    if (musicReady_ && music_.stems[0].frames() > 0) {
        size_t len = music_.stems[0].frames();
        for (int s = 0; s < int(Stem::Count); ++s) {
            float g0 = stemGain_[s];
            float g1 = g0 + (stemTarget_[s] - g0) * 0.04f;  // ~1,5 s para llegar
            stemGain_[s] = g1;
            if (g0 < 1e-4f && g1 < 1e-4f) continue;
            const int16_t* d = music_.stems[s].data.data();
            for (int i = 0; i < frames; ++i) {
                size_t p = (musicPos_ + size_t(i)) % len;
                float g = (g0 + (g1 - g0) * float(i) / float(frames)) / 32768.0f;
                acc[size_t(i) * 2] += float(d[p * 2]) * g;
                acc[size_t(i) * 2 + 1] += float(d[p * 2 + 1]) * g;
            }
        }
        musicPos_ = (musicPos_ + size_t(frames)) % len;
    }
    // Efectos: lectura con interpolación lineal (para cambiar el tono) y panorámica.
    for (Voice& v : voices_) {
        const std::vector<int16_t>& d = v.buf->data;
        size_t n = v.buf->frames();
        float a = (v.pan + 1.0f) * 0.785398f;
        float gl = std::cos(a) * 1.4142f * v.gain / 32768.0f, gr = std::sin(a) * 1.4142f * v.gain / 32768.0f;
        for (int i = 0; i < frames && v.pos < double(n - 1); ++i) {
            size_t p = size_t(v.pos);
            float t = float(v.pos - double(p));
            float l = float(d[p * 2]) * (1 - t) + float(d[p * 2 + 2]) * t;
            float r = float(d[p * 2 + 1]) * (1 - t) + float(d[p * 2 + 3]) * t;
            acc[size_t(i) * 2] += l * gl;
            acc[size_t(i) * 2 + 1] += r * gr;
            v.pos += v.pitch;
        }
    }
    voices_.erase(std::remove_if(voices_.begin(), voices_.end(),
                                 [](const Voice& v) { return v.pos >= double(v.buf->frames() - 1); }),
                  voices_.end());
    float m = master_;
    for (size_t i = 0; i < acc.size(); ++i) out[i] = int16_t(std::tanh(acc[i] * m) * 32000.0f);
}
