#include "synth.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace {

constexpr float kTau = 6.28318530718f;

float midiHz(float m) { return 440.0f * std::pow(2.0f, (m - 69.0f) / 12.0f); }

// Ruido blanco determinista (la música sale siempre igual).
struct Noise {
    uint32_t s = 22222;
    float next() {
        s ^= s << 13;
        s ^= s >> 17;
        s ^= s << 5;
        return float(s) / 2147483648.0f - 1.0f;
    }
};

// Pista en coma flotante (estéreo) sobre la que se suman notas; al final se convierte.
struct Track {
    std::vector<float> l, r;
    bool wrap;  // las notas que se pasan del final vuelven al principio (bucle)
    explicit Track(size_t frames, bool loop = false) : l(frames, 0), r(frames, 0), wrap(loop) {}
    size_t size() const { return l.size(); }
    void add(size_t i, float v, float pan) {  // pan -1..1
        if (i >= l.size()) {
            if (!wrap) return;
            i %= l.size();
        }
        float a = (pan + 1.0f) * 0.25f * kTau * 0.5f;  // ley de potencia constante
        l[i] += v * std::cos(a);
        r[i] += v * std::sin(a);
    }
};

// Filtro paso bajo de un polo (suaviza) y de dos polos en cascada.
struct LowPass {
    float y = 0;
    float process(float x, float cutoff) {
        float a = 1.0f - std::exp(-kTau * cutoff / kSampleRate);
        y += (x - y) * a;
        return y;
    }
};

// Reverberación tipo Schroeder/Freeverb: 4 filtros peine con amortiguación y 2 pasa-todo
// por canal (longitudes distintas a izquierda y derecha para abrir el estéreo). Sobre una
// pista en bucle se pasa dos veces: la primera solo llena las colas, que así envuelven el
// final con el principio.
void reverb(Track& t, float wet, float room, float damp) {
    const int combL[4] = {1116, 1188, 1277, 1356}, apL[2] = {556, 441};
    for (int ch = 0; ch < 2; ++ch) {
        std::vector<float>& x = ch == 0 ? t.l : t.r;
        int spread = ch == 0 ? 0 : 23;
        std::vector<std::vector<float>> comb(4), ap(2);
        int ci[4] = {}, ai[2] = {};
        float filt[4] = {};
        for (int k = 0; k < 4; ++k) comb[size_t(k)].assign(size_t(combL[k] + spread), 0.0f);
        for (int k = 0; k < 2; ++k) ap[size_t(k)].assign(size_t(apL[k] + spread), 0.0f);
        std::vector<float> dry = x;
        int passes = t.wrap ? 2 : 1;
        for (int pass = 0; pass < passes; ++pass)
            for (size_t i = 0; i < x.size(); ++i) {
                float in = dry[i] * 0.3f, out = 0;
                for (int k = 0; k < 4; ++k) {
                    std::vector<float>& b = comb[size_t(k)];
                    float y = b[size_t(ci[k])];
                    filt[k] = y * (1.0f - damp) + filt[k] * damp;
                    b[size_t(ci[k])] = in + filt[k] * room;
                    ci[k] = (ci[k] + 1) % int(b.size());
                    out += y;
                }
                for (int k = 0; k < 2; ++k) {
                    std::vector<float>& b = ap[size_t(k)];
                    float bufout = b[size_t(ai[k])];
                    float y = -out + bufout;
                    b[size_t(ai[k])] = out + bufout * 0.5f;
                    ai[k] = (ai[k] + 1) % int(b.size());
                    out = y;
                }
                if (pass == passes - 1) x[i] = dry[i] + out * wet;
            }
    }
}

// Eco estéreo (rebota de un lado a otro).
void pingPong(Track& t, float delaySec, float feedback, float mix) {
    size_t d = size_t(delaySec * kSampleRate);
    std::vector<float> bl(d, 0), br(d, 0);
    size_t idx = 0;
    int passes = t.wrap ? 2 : 1;
    std::vector<float> dl = t.l, dr = t.r;
    for (int pass = 0; pass < passes; ++pass)
        for (size_t i = 0; i < t.size(); ++i) {
            float el = bl[idx], er = br[idx];
            bl[idx] = dr[i] + er * feedback;  // cruza de canal en cada rebote
            br[idx] = dl[i] + el * feedback;
            idx = (idx + 1) % d;
            if (pass == passes - 1) {
                t.l[i] = dl[i] + el * mix;
                t.r[i] = dr[i] + er * mix;
            }
        }
}

StereoBuffer toBuffer(const Track& t, float gain) {
    StereoBuffer b;
    b.data.resize(t.size() * 2);
    for (size_t i = 0; i < t.size(); ++i) {
        // Saturación suave: nunca recorta de golpe.
        b.data[i * 2] = int16_t(std::tanh(t.l[i] * gain) * 32000.0f);
        b.data[i * 2 + 1] = int16_t(std::tanh(t.r[i] * gain) * 32000.0f);
    }
    return b;
}

size_t at(float seconds) { return size_t(std::max(0.0f, seconds) * kSampleRate); }

// --- Instrumentos ---------------------------------------------------------------------

// Colchón: tres dientes de sierra algo desafinados, filtrados, con ataque y caída lentos.
void padNote(Track& t, float start, float dur, float midi, float gain, float pan) {
    float f = midiHz(midi);
    float ph[3] = {0, 0.33f, 0.66f}, det[3] = {-0.006f, 0.0f, 0.007f};
    LowPass lp1, lp2;
    float rel = 1.4f, len = dur + rel;
    size_t s0 = at(start), n = at(len);
    for (size_t i = 0; i < n; ++i) {
        float tt = float(i) / kSampleRate;
        float env = std::min(1.0f, tt / 0.9f) * (tt > dur ? std::exp(-(tt - dur) / (rel * 0.35f)) : 1.0f);
        float v = 0;
        for (int k = 0; k < 3; ++k) {
            ph[k] += f * (1.0f + det[k]) / kSampleRate;
            ph[k] -= std::floor(ph[k]);
            v += ph[k] * 2.0f - 1.0f;
        }
        float cutoff = 700.0f + 500.0f * (0.5f + 0.5f * std::sin(kTau * (start + tt) * 0.07f));
        v = lp2.process(lp1.process(v, cutoff), cutoff);
        t.add(s0 + i, v * env * gain, pan);
    }
}

// Piano eléctrico / campana: síntesis FM (un seno modula a otro), índice que decae.
void fmNote(Track& t, float start, float midi, float gain, float pan, float ratio, float index, float decay,
            float length) {
    float f = midiHz(midi);
    size_t s0 = at(start), n = at(length);
    float pc = 0, pm = 0;
    for (size_t i = 0; i < n; ++i) {
        float tt = float(i) / kSampleRate;
        float env = std::min(1.0f, tt / 0.004f) * std::exp(-tt / decay);
        float idx = index * std::exp(-tt / (decay * 0.35f)) + 0.15f;
        pm += f * ratio / kSampleRate;
        pc += f / kSampleRate;
        float v = std::sin(kTau * pc + idx * std::sin(kTau * pm));
        t.add(s0 + i, v * env * gain, pan);
    }
}

// Bajo: seno con su octava y algo de saturación (se oye en altavoces pequeños).
void bassNote(Track& t, float start, float dur, float midi, float gain) {
    float f = midiHz(midi);
    size_t s0 = at(start), n = at(dur + 0.1f);
    float ph = 0;
    for (size_t i = 0; i < n; ++i) {
        float tt = float(i) / kSampleRate;
        float env = std::min(1.0f, tt / 0.01f) * std::exp(-tt / (dur * 0.8f)) * (tt > dur ? std::exp(-(tt - dur) / 0.03f) : 1.0f);
        ph += f / kSampleRate;
        float v = std::sin(kTau * ph) + 0.45f * std::sin(2.0f * kTau * ph) + 0.15f * std::sin(3.0f * kTau * ph);
        t.add(s0 + i, std::tanh(v * 1.3f) * env * gain, 0);
    }
}

void kick(Track& t, float start, float gain) {
    size_t s0 = at(start), n = at(0.4f);
    float ph = 0;
    for (size_t i = 0; i < n; ++i) {
        float tt = float(i) / kSampleRate;
        float f = 45.0f + 90.0f * std::exp(-tt / 0.035f);
        ph += f / kSampleRate;
        t.add(s0 + i, std::sin(kTau * ph) * std::exp(-tt / 0.13f) * gain, 0);
    }
}

void clap(Track& t, float start, float gain, Noise& nz) {
    size_t s0 = at(start), n = at(0.3f);
    LowPass lp, hp;
    for (size_t i = 0; i < n; ++i) {
        float tt = float(i) / kSampleRate;
        float x = nz.next();
        float band = lp.process(x - hp.process(x, 900.0f), 3500.0f);  // banda 0,9–3,5 kHz
        float env = std::exp(-tt / 0.07f) * (tt < 0.02f ? 0.7f + 0.3f * std::sin(tt * 900.0f) : 1.0f);
        t.add(s0 + i, band * env * gain * 2.2f, 0.1f);
    }
}

void hat(Track& t, float start, float gain, float pan, Noise& nz) {
    size_t s0 = at(start), n = at(0.06f);
    LowPass lp;
    for (size_t i = 0; i < n; ++i) {
        float tt = float(i) / kSampleRate;
        float x = nz.next();
        t.add(s0 + i, (x - lp.process(x, 6000.0f)) * std::exp(-tt / 0.018f) * gain, pan);
    }
}

void triNote(Track& t, float start, float midi, float gain, float pan, float decay) {
    float f = midiHz(midi);
    size_t s0 = at(start), n = at(decay * 5.0f);
    float ph = 0;
    for (size_t i = 0; i < n; ++i) {
        float tt = float(i) / kSampleRate;
        ph += f / kSampleRate;
        ph -= std::floor(ph);
        float v = 4.0f * std::fabs(ph - 0.5f) - 1.0f;
        t.add(s0 + i, v * std::exp(-tt / decay) * std::min(1.0f, tt / 0.003f) * gain, pan);
    }
}

// --- Las pistas ------------------------------------------------------------------------
// Cada pista: tempo, tónica, modo, progresión (grados de la escala por compás) y
// carácter (instrumento de la melodía y estilo de percusión). La melodía se genera por
// frases a partir de la armonía (con semilla fija: la pista suena siempre igual) en una
// forma A A' B A'' para que no se repita el mismo motivo.

struct TrackDef {
    float bpm;
    int root;          // tónica (MIDI) en la octava del colchón
    int mode[7];       // semitonos de cada grado
    int prog[8];       // grados de la escala, un acorde por compás (se repite)
    int bars;
    int melody;        // 0 celesta, 1 marimba, 2 campanas, 3 metal
    int drums;         // 0 suave, 1 percusión de mano, 2 de empuje (jefe)
    uint32_t seed;
};

// clang-format off
const TrackDef kTracks[int(TrackId::Count)] = {
    // Plaza del Prisma: La menor eólico, íntima y melancólica.
    {72,  57, {0, 2, 3, 5, 7, 8, 10}, {0, 5, 2, 6, 0, 3, 4, 4}, 16, 0, 0, 11},
    // Mercado de los Ecos: Re dórico, más vivo y con calor.
    {96,  50, {0, 2, 3, 5, 7, 9, 10}, {0, 3, 0, 3, 6, 5, 3, 4}, 24, 1, 1, 23},
    // Puente de los Faroles: Fa lidio, abierto, como suspendido en el aire.
    {80,  53, {0, 2, 4, 6, 7, 9, 11}, {0, 1, 0, 1, 5, 4, 3, 4}, 16, 2, 0, 37},
    // Jefe: Do# menor armónico, tenso.
    {128, 49, {0, 2, 3, 5, 7, 8, 11}, {0, 0, 5, 5, 3, 3, 4, 4}, 32, 3, 2, 51},
};
// clang-format on

int scaleNote(const TrackDef& t, int degree) {
    int oct = degree >= 0 ? degree / 7 : -((6 - degree) / 7);
    int d = degree - oct * 7;
    return t.root + 12 * oct + t.mode[d];
}

struct Rng {
    uint32_t s;
    float next() {
        s = s * 1664525u + 1013904223u;
        return float(s >> 8) / float(1u << 24);
    }
};

struct Note {
    float beat, length;
    int midi;
};

// Una frase de 4 compases: ritmos de un repertorio y notas que se mueven por grados,
// cayendo en notas del acorde en los tiempos fuertes. Termina en la fundamental.
std::vector<Note> makePhrase(const TrackDef& t, int firstBar, Rng& rng, bool ending) {
    static const float kRhythms[][5] = {
        {1, 1, 2, 0, 0}, {1.5f, 0.5f, 1, 1, 0}, {2, 1, 1, 0, 0}, {0.5f, 0.5f, 1, 2, 0},
        {1, 0.5f, 0.5f, 2, 0}, {3, 1, 0, 0, 0}, {1, 1, 1, 1, 0}, {0.75f, 0.25f, 1, 2, 0},
    };
    std::vector<Note> out;
    int pos = 7 + t.prog[firstBar % 8];  // una octava por encima de la tónica
    for (int b = 0; b < 4; ++b) {
        int chord = t.prog[(firstBar + b) % 8];
        const float* r = kRhythms[int(rng.next() * 8) % 8];
        float beat = 0;
        if (b == 3) r = kRhythms[ending ? 5 : 2];
        for (int k = 0; k < 5 && r[k] > 0; ++k) {
            bool strong = std::fmod(beat, 2.0f) < 0.01f;
            if (b == 3 && r[k + 1] == 0) {
                pos = 7 + chord;  // final de frase: la fundamental del acorde
            } else if (strong) {
                // La nota del acorde más cercana.
                int best = pos, bestD = 99;
                for (int c : {chord, chord + 2, chord + 4, chord + 7, chord + 9, chord + 11}) {
                    int dd = std::abs(c - pos);
                    if (dd < bestD) bestD = dd, best = c;
                }
                pos = best;
            } else {
                float x = rng.next();
                pos += x < 0.35f ? 1 : x < 0.7f ? -1 : x < 0.85f ? 2 : -2;
            }
            pos = std::clamp(pos, 3, 15);
            bool rest = k > 0 && rng.next() < 0.12f;
            if (!rest) out.push_back({float(firstBar + b) * 4 + beat, r[k], scaleNote(t, pos)});
            beat += r[k];
        }
    }
    return out;
}

void tom(Track& tr, float start, float f0, float gain, float pan) {
    size_t s0 = at(start), n = at(0.3f);
    float ph = 0;
    for (size_t i = 0; i < n; ++i) {
        float tt = float(i) / kSampleRate;
        ph += (f0 * (0.7f + 0.3f * std::exp(-tt / 0.05f))) / kSampleRate;
        tr.add(s0 + i, std::sin(kTau * ph) * std::exp(-tt / 0.12f) * gain, pan);
    }
}

}  // namespace

Music renderMusic(TrackId id) {
    const TrackDef& t = kTracks[int(id)];
    const float beat = 60.0f / t.bpm;
    Music m;
    m.loopSeconds = float(t.bars) * 4 * beat;
    const size_t frames = at(m.loopSeconds);
    Noise nz;
    Rng rng{t.seed};
    auto chordNotes = [&](int bar, int* out) {
        int d = t.prog[bar % 8];
        out[0] = scaleNote(t, d);
        out[1] = scaleNote(t, d + 2);
        out[2] = scaleNote(t, d + 4);
    };

    // Colchón: el acorde de cada compás.
    Track pad(frames, true);
    for (int b = 0; b < t.bars; ++b) {
        int n[3];
        chordNotes(b, n);
        for (int k = 0; k < 3; ++k)
            padNote(pad, float(b) * 4 * beat, 4 * beat, float(n[k]), t.drums == 2 ? 0.045f : 0.06f, -0.5f + 0.5f * float(k));
    }
    reverb(pad, t.melody == 2 ? 0.75f : 0.6f, 0.86f, 0.4f);
    m.stems[int(Stem::Pad)] = toBuffer(pad, 1.0f);

    // Melodía: frases A A' B A'' (cada una de 4 compases; las pistas largas siguen).
    Track mel(frames, true);
    std::vector<Note> notes;
    Rng phraseA = rng, phraseB{t.seed * 7u + 3u};
    for (int p = 0; p * 4 < t.bars; ++p) {
        int kind = p % 4;  // 0 A, 1 A', 2 B, 3 A''
        Rng r = kind == 2 ? phraseB : phraseA;
        if (kind == 1 || kind == 3) r.s ^= uint32_t(p) * 2654435761u;  // la misma idea, con otro final
        if (p >= 4) r.s += uint32_t(p) * 977u;                          // pistas largas: frases nuevas
        std::vector<Note> ph = makePhrase(t, p * 4, r, kind == 3);
        notes.insert(notes.end(), ph.begin(), ph.end());
    }
    for (const Note& n : notes) {
        float st = n.beat * beat;
        switch (t.melody) {
            case 0: fmNote(mel, st, float(n.midi), 0.13f, 0.1f, 2.0f, 1.2f, 0.7f, 3.0f); break;               // celesta
            case 1: fmNote(mel, st, float(n.midi), 0.17f, -0.1f, 4.0f, 2.5f, 0.22f, 1.2f); break;             // marimba
            case 2: fmNote(mel, st, float(n.midi), 0.11f, 0.15f, 3.5f, 1.8f, 1.3f, 5.0f); break;              // campanas
            default:                                                                                         // metal
                fmNote(mel, st, float(n.midi), 0.09f, -0.2f, 1.0f, 3.0f, 0.35f * n.length * beat + 0.1f, 2.0f);
                fmNote(mel, st, float(n.midi - 12), 0.07f, 0.2f, 1.0f, 2.0f, 0.35f * n.length * beat + 0.1f, 2.0f);
                break;
        }
    }
    pingPong(mel, beat * 0.75f, t.melody == 1 ? 0.25f : 0.35f, 0.3f);
    reverb(mel, t.melody == 2 ? 0.6f : 0.45f, 0.84f, 0.35f);
    m.stems[int(Stem::Melody)] = toBuffer(mel, 1.0f);

    // Pulso (combate): bajo y percusión según el estilo de la pista.
    Track pulse(frames, true);
    for (int b = 0; b < t.bars; ++b) {
        float t0 = float(b) * 4 * beat;
        int n[3];
        chordNotes(b, n);
        float root = float(n[0] - 12);
        if (t.drums == 0) {  // suave: bajo largo, bombo en el uno, charles en las partes débiles
            bassNote(pulse, t0, 2.5f * beat, root, 0.18f);
            bassNote(pulse, t0 + 2.5f * beat, 1.4f * beat, root + 7, 0.12f);
            kick(pulse, t0, 0.45f);
            if (b % 2 == 1) clap(pulse, t0 + 3 * beat, 0.1f, nz);
            for (int h = 0; h < 4; ++h) hat(pulse, t0 + (float(h) + 0.5f) * beat, 0.04f, 0.3f, nz);
        } else if (t.drums == 1) {  // mano: bajo punteado y tambores en 3-3-2
            for (float o : {0.0f, 1.5f, 3.0f}) bassNote(pulse, t0 + o * beat, 0.5f * beat, root + (o == 3.0f ? 7.0f : 0.0f), 0.16f);
            const float pattern[] = {0, 0.75f, 1.5f, 2.25f, 3.0f, 3.5f};
            for (int k = 0; k < 6; ++k) tom(pulse, t0 + pattern[k] * beat, k % 3 == 0 ? 180.0f : 260.0f, 0.22f, k % 2 ? 0.4f : -0.4f);
            for (int h = 0; h < 16; ++h) hat(pulse, t0 + float(h) * 0.25f * beat, h % 4 == 2 ? 0.05f : 0.025f, 0.2f, nz);
        } else {  // empuje: bajo en corcheas, bombo a negras, palmadas y redobles cada 4 compases
            for (int k = 0; k < 8; ++k) bassNote(pulse, t0 + float(k) * 0.5f * beat, 0.4f * beat, root + (k == 7 ? 12.0f : 0.0f), 0.15f);
            for (int k = 0; k < 4; ++k) kick(pulse, t0 + float(k) * beat, 0.5f);
            clap(pulse, t0 + beat, 0.15f, nz);
            clap(pulse, t0 + 3 * beat, 0.15f, nz);
            for (int h = 0; h < 16; ++h) hat(pulse, t0 + float(h) * 0.25f * beat, h % 2 ? 0.05f : 0.03f, h % 2 ? 0.3f : -0.3f, nz);
            if (b % 4 == 3)
                for (int k = 0; k < 4; ++k) tom(pulse, t0 + (3.0f + 0.25f * float(k)) * beat, 220.0f - 30.0f * float(k), 0.25f, 0);
        }
    }
    reverb(pulse, 0.15f, 0.7f, 0.5f);
    m.stems[int(Stem::Pulse)] = toBuffer(pulse, 1.0f);

    // Brillo: arpegio sobre el acorde (semicorcheas o corcheas según el tempo).
    Track shim(frames, true);
    const int pattern[8] = {0, 1, 2, 1, 0, 2, 1, 2};
    int perBar = t.bpm > 100 ? 8 : 16;
    for (int b = 0; b < t.bars; ++b) {
        int n[3];
        chordNotes(b, n);
        for (int i = 0; i < perBar; ++i) {
            int note = n[pattern[i % 8]] + 12 + (i >= perBar / 2 ? 12 : 0);
            triNote(shim, float(b) * 4 * beat + float(i) * (4.0f / float(perBar)) * beat, float(note), 0.06f,
                    i % 2 ? 0.6f : -0.6f, 0.12f);
        }
    }
    pingPong(shim, beat * 0.5f, 0.4f, 0.4f);
    reverb(shim, 0.7f, 0.88f, 0.3f);
    m.stems[int(Stem::Shimmer)] = toBuffer(shim, 1.0f);
    return m;
}

// --- Efectos de sonido ------------------------------------------------------------------

StereoBuffer renderSfx(Sfx id) {
    Noise nz;
    nz.s = 777u + uint32_t(id) * 131u;
    auto len = [](float s) { return at(s); };
    Track t(len(2.0f));
    // Soplido: ruido filtrado en banda que barre de f0 a f1.
    auto whoosh = [&](float start, float dur, float f0, float f1, float gain, float pan) {
        LowPass lp, hp;
        size_t s0 = at(start), n = at(dur);
        for (size_t i = 0; i < n; ++i) {
            float u = float(i) / float(n);
            float fc = f0 + (f1 - f0) * u;
            float x = nz.next();
            float band = lp.process(x - hp.process(x, fc * 0.5f), fc * 1.5f);
            t.add(s0 + i, band * std::sin(u * 3.14159f) * gain, pan);
        }
    };
    // Golpe sordo: seno que cae de tono.
    auto thump = [&](float start, float f0, float f1, float decay, float gain) {
        size_t s0 = at(start), n = at(decay * 5.0f);
        float ph = 0;
        for (size_t i = 0; i < n; ++i) {
            float tt = float(i) / kSampleRate;
            ph += (f1 + (f0 - f1) * std::exp(-tt / (decay * 0.4f))) / kSampleRate;
            t.add(s0 + i, std::sin(kTau * ph) * std::exp(-tt / decay) * gain, 0);
        }
    };
    auto burst = [&](float start, float dur, float cutoff, float gain) {
        LowPass lp;
        size_t s0 = at(start), n = at(dur);
        for (size_t i = 0; i < n; ++i) {
            float tt = float(i) / kSampleRate;
            float x = nz.next();
            float v = cutoff > 0 ? lp.process(x, cutoff) : x - lp.process(x, -cutoff);
            t.add(s0 + i, v * std::exp(-tt / (dur * 0.3f)) * gain, 0);
        }
    };
    auto bell = [&](float start, float midi, float gain, float decay, float pan = 0) {
        fmNote(t, start, midi, gain, pan, 3.5f, 1.6f, decay, decay * 4.0f);
    };
    float wet = 0.15f;
    switch (id) {
        case Sfx::Slash: whoosh(0, 0.2f, 2600, 700, 0.9f, 0.2f); break;
        case Sfx::Whip: whoosh(0, 0.28f, 3500, 900, 0.9f, -0.2f); burst(0.24f, 0.04f, -3000, 0.6f); break;
        case Sfx::Hammer: whoosh(0, 0.3f, 900, 300, 0.9f, 0); thump(0.25f, 140, 45, 0.18f, 0.9f); wet = 0.3f; break;
        case Sfx::Hit:
            thump(0, 170, 60, 0.05f, 0.8f);
            burst(0, 0.05f, -2500, 0.5f);
            bell(0, 88, 0.12f, 0.12f);  // los Grises suenan a cristal
            break;
        case Sfx::HitHeavy:
            thump(0, 150, 40, 0.12f, 1.0f);
            burst(0, 0.12f, 1800, 0.8f);
            bell(0, 83, 0.15f, 0.2f);
            wet = 0.3f;
            break;
        case Sfx::Jump: whoosh(0, 0.15f, 500, 1400, 0.35f, 0); break;
        case Sfx::Dodge: whoosh(0, 0.28f, 600, 2200, 0.6f, 0); break;
        case Sfx::Guard: fmNote(t, 0, 79, 0.35f, 0, 1.41f, 6.0f, 0.12f, 0.6f); burst(0, 0.03f, -4000, 0.4f); break;
        case Sfx::PerfectGuard:
            bell(0, 86, 0.3f, 0.5f, -0.3f);
            bell(0.03f, 93, 0.25f, 0.5f, 0.3f);
            wet = 0.5f;
            break;
        case Sfx::HeroHurt: thump(0, 110, 50, 0.1f, 0.9f); burst(0, 0.08f, 900, 0.5f); break;
        case Sfx::EnemyWindup: {  // zumbido de estática que sube: aviso del ataque
            size_t n = len(0.35f);
            float ph = 0;
            for (size_t i = 0; i < n; ++i) {
                float tt = float(i) / kSampleRate;
                ph += (180.0f + 500.0f * tt) / kSampleRate;
                float sq = std::sin(kTau * ph) > 0 ? 1.0f : -1.0f;
                t.add(i, (sq * 0.3f + nz.next() * 0.4f) * std::min(1.0f, tt / 0.3f) * 0.25f, 0);
            }
            break;
        }
        case Sfx::EnemyDie:  // chasquido de estática y el color que vuelve: arpegio que sube
            burst(0, 0.08f, -2000, 0.6f);
            for (int i = 0; i < 4; ++i) bell(0.06f + 0.06f * float(i), float(74 + (i == 0 ? 0 : i == 1 ? 4 : i == 2 ? 7 : 12)), 0.16f, 0.35f, -0.4f + 0.25f * float(i));
            wet = 0.45f;
            break;
        case Sfx::Pickup: bell(0, 86, 0.18f, 0.12f); break;
        case Sfx::Coin: bell(0, 95, 0.14f, 0.07f); bell(0.06f, 100, 0.14f, 0.12f); break;
        case Sfx::ItemGet:
            bell(0, 81, 0.2f, 0.3f, -0.3f);
            bell(0.09f, 85, 0.2f, 0.3f, 0);
            bell(0.18f, 88, 0.22f, 0.5f, 0.3f);
            wet = 0.4f;
            break;
        case Sfx::Fire: whoosh(0, 0.45f, 300, 1200, 0.8f, 0); break;
        case Sfx::FireBoom: burst(0, 0.6f, 700, 1.1f); thump(0, 90, 35, 0.2f, 0.9f); wet = 0.3f; break;
        case Sfx::Ice:
            for (int i = 0; i < 5; ++i) bell(0.03f * float(i), float(96 - i * 2), 0.1f, 0.15f, -0.5f + 0.25f * float(i));
            wet = 0.35f;
            break;
        case Sfx::Cure:
            for (int i = 0; i < 5; ++i) bell(0.07f * float(i), float(74 + i * 3 + (i > 2 ? 1 : 0)), 0.13f, 0.5f, -0.4f + 0.2f * float(i));
            wet = 0.5f;
            break;
        case Sfx::Chest:
            for (int i = 0; i < 6; ++i) bell(0.05f * float(i), float(69 + i * 2 + (i > 3 ? 1 : 0)), 0.14f, 0.5f, -0.5f + 0.2f * float(i));
            bell(0.35f, 93, 0.2f, 0.8f);
            wet = 0.5f;
            break;
        case Sfx::Crate: burst(0, 0.12f, -1500, 0.6f); bell(0, 91, 0.12f, 0.1f); thump(0, 200, 90, 0.05f, 0.4f); break;
        case Sfx::WaveStart: {  // oleaje grave que crece, con estática
            size_t n = len(1.2f);
            float ph = 0;
            LowPass lp;
            for (size_t i = 0; i < n; ++i) {
                float tt = float(i) / kSampleRate;
                ph += 55.0f / kSampleRate;
                float env = std::sin(std::min(1.0f, tt / 1.2f) * 3.14159f);
                t.add(i, (std::sin(kTau * ph) * 0.6f + lp.process(nz.next(), 400.0f) * 0.8f) * env * 0.5f, 0);
            }
            wet = 0.4f;
            break;
        }
        case Sfx::Echo: {  // acorde que crece desde la nada (como un sonido al revés)
            const int notes[4] = {62, 66, 69, 74};
            for (int k = 0; k < 4; ++k) {
                float f = midiHz(float(notes[k] + 12));
                size_t n = len(1.4f);
                float ph = 0;
                for (size_t i = 0; i < n; ++i) {
                    float tt = float(i) / kSampleRate;
                    ph += f / kSampleRate;
                    float env = tt < 1.0f ? tt * tt : std::exp(-(tt - 1.0f) / 0.1f);
                    t.add(i, std::sin(kTau * ph) * env * 0.07f, -0.6f + 0.4f * float(k));
                }
            }
            wet = 0.6f;
            break;
        }
        case Sfx::Blip: triNote(t, 0, 84, 0.08f, 0, 0.012f); break;
        case Sfx::MenuMove: triNote(t, 0, 88, 0.1f, 0, 0.015f); break;
        case Sfx::MenuOk: triNote(t, 0, 83, 0.1f, 0, 0.03f); triNote(t, 0.05f, 90, 0.1f, 0, 0.05f); break;
        case Sfx::Count: break;
    }
    if (wet > 0) reverb(t, wet, 0.8f, 0.4f);
    // Recorta el silencio del final.
    size_t last = 0;
    for (size_t i = 0; i < t.size(); ++i)
        if (std::fabs(t.l[i]) + std::fabs(t.r[i]) > 1e-4f) last = i;
    t.l.resize(last + 1);
    t.r.resize(last + 1);
    return toBuffer(t, 1.0f);
}

// --- WAV (PCM 16 bits estéreo) ---------------------------------------------------------

bool saveWav(const std::string& path, const StereoBuffer& buf) {
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    uint32_t dataBytes = uint32_t(buf.data.size() * 2), rate = kSampleRate, byteRate = rate * 4, riff = 36 + dataBytes;
    uint16_t pcm = 1, ch = 2, align = 4, bits = 16;
    uint32_t fmtLen = 16;
    std::fwrite("RIFF", 1, 4, f);
    std::fwrite(&riff, 4, 1, f);
    std::fwrite("WAVEfmt ", 1, 8, f);
    std::fwrite(&fmtLen, 4, 1, f);
    std::fwrite(&pcm, 2, 1, f);
    std::fwrite(&ch, 2, 1, f);
    std::fwrite(&rate, 4, 1, f);
    std::fwrite(&byteRate, 4, 1, f);
    std::fwrite(&align, 2, 1, f);
    std::fwrite(&bits, 2, 1, f);
    std::fwrite("data", 1, 4, f);
    std::fwrite(&dataBytes, 4, 1, f);
    bool ok = std::fwrite(buf.data.data(), 2, buf.data.size(), f) == buf.data.size();
    std::fclose(f);
    return ok;
}

bool loadWav(const std::string& path, StereoBuffer& buf) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    char hdr[44];
    bool ok = std::fread(hdr, 1, 44, f) == 44 && std::memcmp(hdr, "RIFF", 4) == 0 && std::memcmp(hdr + 36, "data", 4) == 0;
    uint32_t bytes = 0;
    if (ok) std::memcpy(&bytes, hdr + 40, 4);
    if (ok) {
        buf.data.resize(bytes / 2);
        ok = std::fread(buf.data.data(), 2, buf.data.size(), f) == buf.data.size();
    }
    std::fclose(f);
    return ok;
}
