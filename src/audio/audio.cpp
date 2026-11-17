#include "audio/audio.h"

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
constexpr int kMusicVersion = 3;  // cambiarlo regenera la caché si cambia la música

// Las pistas grabadas (assets/audio/musica): su archivo y la sintetizada que suena si no se
// puede leer. Farolas va aparte (dos capas).
struct Recorded {
    TrackId id;
    const char* file;
    TrackId fallback;
};
const Recorded kRecorded[] = {
    {TrackId::Ciudad, "ciudad_loop.ogg", TrackId::Mercado},
    {TrackId::Combate, "combate_loop.ogg", TrackId::Jefe},
    {TrackId::Fonda, "fonda_loop.ogg", TrackId::Plaza},
    {TrackId::JefeGrabado, "jefe_loop.ogg", TrackId::Jefe},
    {TrackId::PianoNocturno, "piano_nocturno_de_las_farolas.ogg", TrackId::Plaza},
    {TrackId::PianoVelaHollin, "piano_vela_y_hollin.ogg", TrackId::Plaza},
    {TrackId::PianoUltimoFarol, "piano_el_ultimo_farol.ogg", TrackId::Plaza},
};
const char* const kStingerFiles[] = {"stinger_victoria.ogg", "stinger_derrota.ogg"};

// Las músicas de zona siguen por donde iban al volver a ellas; las demás empiezan de cero.
bool resumes(int id) { return id == int(TrackId::Farolas) || id == int(TrackId::Ciudad); }

void makeDirs(const std::string& path) {
    for (size_t i = 1; i <= path.size(); ++i)
        if (i == path.size() || path[i] == '/') mkdir(path.substr(0, i).c_str(), 0755);
}

}  // namespace

bool Audio::init(const std::string& cacheDir, const std::string& voiceDir, const std::string& musicDir) {
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
    for (int i = 0; i < int(Sfx::Count); ++i) sfx_.push_back(sfxVariants(Sfx(i), voiceDir));
    lastTake_.assign(sfx_.size(), -1);
    voices_.reserve(64);
    running_ = true;
    thread_ = std::thread(&Audio::run, this);
    cacheDir_ = cacheDir;
    musicDir_ = musicDir;
    for (int i = 0; i < int(Stinger::Count); ++i)
        if (!loadOgg(musicDir + "/" + kStingerFiles[i], stingers_[i])) stingers_[i] = {};
    loader_ = std::thread(&Audio::loaderLoop, this);
    return true;
}

// Prepara las pistas que se piden: de la caché si ya se generaron; si no, se sintetizan
// (unos segundos) y se guardan para la próxima vez.
void Audio::loaderLoop() {
    while (running_) {
        int id = -1;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            loaderCv_.wait(lock, [&] { return !running_ || (wanted_ >= 0 && !tracks_.count(wanted_)); });
            if (!running_) return;
            id = wanted_;
        }
        auto m = std::make_unique<Music>();
        int key = id;  // dónde se guarda (una grabada que no se lee se guarda con su sintetizada)
        bool recorded = false, ok = false;
        if (id == int(TrackId::Farolas)) {  // el tema de Vela y Hollín: luz y sombra
            recorded = true;
            ok = loadOgg(musicDir_ + "/farolas_capa_luz_loop.ogg", m->stems[int(Stem::Pad)]) &&
                 loadOgg(musicDir_ + "/farolas_capa_sombra_loop.ogg", m->stems[int(Stem::Melody)]) &&
                 m->stems[int(Stem::Pad)].frames() == m->stems[int(Stem::Melody)].frames();
            if (!ok) id = int(TrackId::Plaza);
        }
        for (const Recorded& r : kRecorded)
            if (id == int(r.id)) {
                recorded = true;
                ok = loadOgg(musicDir_ + "/" + r.file, m->stems[int(Stem::Pad)]);
                if (!ok) id = int(r.fallback);
            }
        if (recorded && !ok) {
            std::fprintf(stderr, "Audio: no se pudo leer la pista grabada %d; suena una sintetizada\n", key);
            *m = Music{};
        }
        if (ok) {
            m->loopSeconds = float(m->stems[0].frames()) / float(kSampleRate);
        } else {
            bool cached = true;
            auto path = [&](int stem) {
                char name[96];
                std::snprintf(name, sizeof(name), "/musica_v%d_pista%d_capa%d.wav", kMusicVersion, id, stem);
                return cacheDir_ + name;
            };
            for (int st = 0; st < int(Stem::Count) && cached; ++st) cached = loadWav(path(st), m->stems[st]);
            if (!cached) {
                *m = renderMusic(TrackId(id));
                makeDirs(cacheDir_);
                for (int st = 0; st < int(Stem::Count); ++st) saveWav(path(st), m->stems[st]);
            }
        }
        std::lock_guard<std::mutex> lock(mutex_);
        tracks_[key] = std::move(m);
        if (wanted_ == key && current_ != key) {
            previous_ = current_;
            current_ = key;
            fade_ = 0;
        }
    }
}

void Audio::playTrack(TrackId tid) {
    int id = int(tid);
    std::lock_guard<std::mutex> lock(mutex_);
    if (!running_ || wanted_ == id) return;
    wanted_ = id;
    if (tracks_.count(id)) {
        if (current_ != id) {
            // Si vuelve la que se estaba apagando, sigue donde iba; si no, desde el principio
            // (o desde donde se dejó, si es música de zona).
            if (id != previous_ && !resumes(id)) tracks_[id]->pos = 0;
            previous_ = current_;
            current_ = id;
            fade_ = 0;
        }
    } else {
        loaderCv_.notify_one();
    }
}

void Audio::shutdown() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        running_ = false;
    }
    loaderCv_.notify_all();
    if (loader_.joinable()) loader_.join();
    if (thread_.joinable()) thread_.join();
    if (stream_ && paFree) paFree(stream_);
    stream_ = nullptr;
    if (lib_) dlclose(lib_);
    lib_ = nullptr;
}

void Audio::play(Sfx id, float gain, float pan, float pitch) {
    if (!running_ || int(id) >= int(sfx_.size()) || sfx_[size_t(id)].empty()) return;
    std::lock_guard<std::mutex> lock(mutex_);
    // Una toma al azar, distinta de la última si hay más de una.
    const std::vector<StereoBuffer>& takes = sfx_[size_t(id)];
    rng_ = rng_ * 1664525u + 1013904223u;
    int take = int((rng_ >> 8) % uint32_t(takes.size()));
    if (takes.size() > 1 && take == lastTake_[size_t(id)]) take = (take + 1) % int(takes.size());
    lastTake_[size_t(id)] = take;
    if (takes[size_t(take)].frames() < 2) return;
    if (voices_.size() >= 48) voices_.erase(voices_.begin());  // se corta la más antigua
    voices_.push_back({&takes[size_t(take)], 0.0, gain, clampf(pan, -1, 1), pitch});
}

void Audio::playStinger(Stinger id) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!running_ || stingers_[int(id)].frames() == 0) return;
    stinger_ = int(id);
    stingerPos_ = 0;
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

void Audio::mixTrack(Music& m, float* acc, int frames, const float* g0, const float* g1) {
    size_t len = m.stems[0].frames();
    for (int st = 0; st < int(Stem::Count); ++st) {
        if ((g0[st] < 1e-4f && g1[st] < 1e-4f) || m.stems[st].frames() != len) continue;  // capas que la pista no tiene
        const int16_t* d = m.stems[st].data.data();
        for (int i = 0; i < frames; ++i) {
            size_t p = (m.pos + size_t(i)) % len;
            float g = (g0[st] + (g1[st] - g0[st]) * float(i) / float(frames)) / 32768.0f;
            acc[i * 2] += float(d[p * 2]) * g;
            acc[i * 2 + 1] += float(d[p * 2 + 1]) * g;
        }
    }
    m.pos = (m.pos + size_t(frames)) % len;
}

void Audio::mix(int16_t* out, int frames) {
    std::vector<float> acc(size_t(frames) * 2, 0.0f);
    std::lock_guard<std::mutex> lock(mutex_);
    // Música: las capas a la vez, cada una con su volumen (rampa suave por bloque); la
    // pista anterior se apaga mientras entra la nueva.
    float f0 = fade_;
    fade_ = std::min(1.0f, fade_ + float(frames) / (2.0f * kSampleRate));
    float g0[int(Stem::Count)], g1[int(Stem::Count)];
    for (int st = 0; st < int(Stem::Count); ++st) {
        g0[st] = stemGain_[st];
        stemGain_[st] += (stemTarget_[st] - stemGain_[st]) * 0.04f;  // ~1,5 s para llegar
        g1[st] = stemGain_[st];
    }
    auto play = [&](int id, float a0, float a1) {
        auto it = tracks_.find(id);
        if (it == tracks_.end() || it->second->stems[0].frames() == 0) return;
        float s0[int(Stem::Count)], s1[int(Stem::Count)];
        for (int st = 0; st < int(Stem::Count); ++st) {
            s0[st] = g0[st] * a0;
            s1[st] = g1[st] * a1;
        }
        mixTrack(*it->second, acc.data(), frames, s0, s1);
    };
    if (current_ >= 0) play(current_, f0, fade_);
    if (previous_ >= 0 && f0 < 1.0f) play(previous_, 1.0f - f0, 1.0f - fade_);
    // Fragmento suelto: la música baja mientras suena (y vuelve despacio al acabar).
    float d0 = duck_;
    duck_ += ((stinger_ >= 0 ? 0.25f : 1.0f) - duck_) * (stinger_ >= 0 ? 0.3f : 0.03f);
    for (int i = 0; i < frames; ++i) {
        float d = d0 + (duck_ - d0) * float(i) / float(frames);
        acc[size_t(i) * 2] *= d;
        acc[size_t(i) * 2 + 1] *= d;
    }
    if (stinger_ >= 0) {
        const StereoBuffer& b = stingers_[stinger_];
        for (int i = 0; i < frames && stingerPos_ < b.frames(); ++i, ++stingerPos_) {
            acc[size_t(i) * 2] += float(b.data[stingerPos_ * 2]) * (0.9f / 32768.0f);
            acc[size_t(i) * 2 + 1] += float(b.data[stingerPos_ * 2 + 1]) * (0.9f / 32768.0f);
        }
        if (stingerPos_ >= b.frames()) stinger_ = -1;
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
