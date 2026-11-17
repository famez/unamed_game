#include "audio/synth.h"

#include "audio/barks.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <dlfcn.h>

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

// --- Voz: síntesis por formantes -------------------------------------------------------------
// Un pulso glotal (Rosenberg, derivado: la radiación de los labios) a la frecuencia de la voz
// más aliento (ruido), por resonadores en paralelo en los formantes de cada vocal. Las
// consonantes son eso mismo sin voz (h, j), con la voz cortada a golpes (r vibrante) o un
// chasquido (t).

struct Phone {
    float f1, f2, f3;
    float dur;
    float voiced = 1;  // 0 = solo aliento
    float breath = 0;  // aliento añadido
    float roll = 0;    // r vibrante: la voz se corta ~28 veces por segundo
    float gain = 1;
};
// Vocales de una voz femenina (Hz).
const float kA[3] = {900, 1400, 2900}, kE[3] = {560, 2100, 2950}, kI[3] = {330, 2700, 3300}, kO[3] = {560, 960, 2800},
            kU[3] = {390, 900, 2700}, kAh[3] = {720, 1300, 2800};
Phone ph(const float* v, float dur, float voiced = 1, float breath = 0, float roll = 0, float gain = 1) {
    return {v[0], v[1], v[2], dur, voiced, breath, roll, gain};
}

struct VoiceStyle {
    float f0[4];        // entonación: inicio, 30 %, 70 %, final (Hz)
    float breathy = 0.1f;
    float fry = 0;      // carraspeo: periodos alternos más flojos (subarmónico) y algo de jitter
    float drive = 1;    // saturación (más cañera)
    float bright = 1;   // peso del tercer y cuarto formante
    float pan = 0;
};

struct Resonator {
    float y1 = 0, y2 = 0;
    float process(float x, float f, float bw) {
        float r = std::exp(-3.14159f * bw / kSampleRate);
        float a1 = 2.0f * r * std::cos(kTau * f / kSampleRate), a2 = -r * r;
        float y = (1.0f - r) * x + a1 * y1 + a2 * y2;
        y2 = y1;
        y1 = y;
        return y;
    }
};

void voice(Track& t, Noise& nz, float start, const std::vector<Phone>& phones, const VoiceStyle& st, float gain) {
    float total = 0;
    for (const Phone& p : phones) total += p.dur;
    size_t s0 = at(start), n = at(total);
    std::vector<float> out(n, 0.0f);
    Resonator r1, r2, r3, r4;
    LowPass hp;
    float phase = 0, prevG = 0, jit = 1;
    int period = 0;
    for (size_t i = 0; i < n; ++i) {
        float tt = float(i) / kSampleRate, u = tt / total;
        // Fonema actual y el siguiente (los formantes se deslizan en el último 35 % del fonema).
        float acc = 0;
        size_t k = 0;
        while (k + 1 < phones.size() && tt > acc + phones[k].dur) acc += phones[k++].dur;
        const Phone& a = phones[k];
        const Phone& b = k + 1 < phones.size() ? phones[k + 1] : a;
        float local = (tt - acc) / a.dur, m = smoothstep01((local - 0.65f) / 0.35f);
        float f1 = lerpf(a.f1, b.f1, m), f2 = lerpf(a.f2, b.f2, m), f3 = lerpf(a.f3, b.f3, m);
        float voiced = lerpf(a.voiced, b.voiced, m * 0.5f), breath = a.breath + st.breathy;
        float pg = lerpf(a.gain, b.gain, m);
        // Entonación por tramos.
        const float knots[4] = {0, 0.3f, 0.7f, 1};
        int seg = u < 0.3f ? 0 : u < 0.7f ? 1 : 2;
        float f0 = lerpf(st.f0[seg], st.f0[seg + 1], smoothstep01((u - knots[seg]) / (knots[seg + 1] - knots[seg])));
        f0 *= 1.0f + 0.012f * std::sin(kTau * 5.5f * tt);  // un poco de vibrato
        phase += f0 * jit / kSampleRate;
        if (phase >= 1.0f) {
            phase -= 1.0f;
            ++period;
            jit = 1.0f + st.fry * 0.06f * nz.next();
        }
        // Pulso de Rosenberg: abre (40 %), cierra (16 %), cerrado el resto.
        float g = phase < 0.4f ? 0.5f * (1.0f - std::cos(3.14159f * phase / 0.4f))
                  : phase < 0.56f ? std::cos(1.5708f * (phase - 0.4f) / 0.16f) : 0.0f;
        float dg = (g - prevG) * 18.0f;
        prevG = g;
        if (st.fry > 0 && (period & 1)) dg *= 1.0f - 0.55f * st.fry;
        if (a.roll > 0) dg *= 0.35f + 0.65f * (0.5f + 0.5f * std::sin(kTau * 28.0f * tt));
        float noise = nz.next();
        float x = dg * voiced + noise * breath * 0.6f;
        float y = r1.process(x, f1, 90) * 1.0f + r2.process(x, f2, 110) * 0.7f + r3.process(x, f3, 160) * 0.35f * st.bright +
                  r4.process(x, 3900, 250) * 0.18f * st.bright;
        y -= hp.process(y, 120.0f);  // sin retumbe
        // Envolvente: ataque rápido y caída al final de la palabra.
        float env = std::min(1.0f, tt / 0.012f) * std::min(1.0f, (total - tt) / 0.05f);
        out[i] = y * env * pg;
    }
    float peak = 1e-6f;
    for (float v : out) peak = std::max(peak, std::fabs(v));
    float norm = 1.0f / std::tanh(st.drive);
    for (size_t i = 0; i < n; ++i) {
        float v = out[i] / peak;
        if (st.drive > 1.0f) v = std::tanh(v * st.drive) * norm;
        t.add(s0 + i, v * gain, st.pan);
    }
}

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
    // Chasquido de impacto: ruido muy agudo de pocos milisegundos (el "clac" del contacto).
    auto click = [&](float start, float gain) {
        LowPass lp;
        size_t s0 = at(start), n = at(0.012f);
        for (size_t i = 0; i < n; ++i) {
            float tt = float(i) / kSampleRate, x = nz.next();
            t.add(s0 + i, (x - lp.process(x, 3000.0f)) * std::exp(-tt / 0.0025f) * gain, 0);
        }
    };
    // Crujido: ruido en banda, áspero (se satura al final con el resto).
    auto crunch = [&](float start, float dur, float fc, float gain, float pan = 0) {
        LowPass lp, hp;
        size_t s0 = at(start), n = at(dur);
        for (size_t i = 0; i < n; ++i) {
            float tt = float(i) / kSampleRate, x = nz.next();
            float band = lp.process(x - hp.process(x, fc * 0.4f), fc * 1.6f);
            float grain = 0.6f + 0.4f * nz.next();  // textura granulada
            t.add(s0 + i, band * grain * std::exp(-tt / (dur * 0.35f)) * gain, pan);
        }
    };
    // Timbre metálico: parciales inarmónicos que se apagan rápido (filo contra algo duro).
    auto metal = [&](float start, float f, float decay, float gain, float pan = 0) {
        const float ratios[4] = {1.0f, 2.76f, 5.4f, 8.93f}, amps[4] = {1.0f, 0.6f, 0.35f, 0.2f};
        size_t s0 = at(start), n = at(decay * 5.0f);
        for (size_t i = 0; i < n; ++i) {
            float tt = float(i) / kSampleRate, v = 0;
            for (int k = 0; k < 4; ++k) v += std::sin(kTau * f * ratios[k] * tt) * amps[k] * std::exp(-tt * (1.0f + float(k)) / decay);
            t.add(s0 + i, v * gain * 0.4f, pan);
        }
    };
    // Grave sostenido (sub): da peso a los golpes fuertes y a las explosiones.
    auto sub = [&](float start, float f0, float f1, float dur, float gain) {
        size_t s0 = at(start), n = at(dur);
        float ph = 0;
        for (size_t i = 0; i < n; ++i) {
            float u = float(i) / float(n);
            ph += (f0 + (f1 - f0) * u) / kSampleRate;
            float env = std::min(1.0f, u * 40.0f) * (1.0f - u) * (1.0f - u);
            t.add(s0 + i, std::sin(kTau * ph) * env * gain, 0);
        }
    };
    // Soplido al revés: crece y se corta en seco (absorber, aparecer).
    auto swell = [&](float start, float dur, float fc, float gain) {
        LowPass lp;
        size_t s0 = at(start), n = at(dur);
        for (size_t i = 0; i < n; ++i) {
            float u = float(i) / float(n);
            t.add(s0 + i, lp.process(nz.next(), fc * (0.3f + u)) * u * u * gain, 0);
        }
    };
    float wet = 0.12f, drive = 1.0f;  // drive > 1: saturación (más agresivo)
    switch (id) {
        case Sfx::Slash: whoosh(0, 0.16f, 3800, 900, 1.2f, 0.2f); click(0.13f, 0.25f); break;
        case Sfx::Whip: whoosh(0, 0.22f, 4200, 700, 1.2f, -0.2f); click(0.2f, 0.5f); break;
        case Sfx::Hammer: whoosh(0, 0.26f, 1200, 250, 1.1f, 0); sub(0.22f, 90, 38, 0.3f, 0.9f); crunch(0.22f, 0.15f, 700, 0.8f); drive = 2.0f; break;
        case Sfx::Hit:  // golpe seco con cuerpo y filo
            click(0, 0.9f);
            thump(0, 190, 55, 0.045f, 1.0f);
            crunch(0, 0.09f, 1600, 0.9f);
            metal(0.002f, 610, 0.05f, 0.35f);
            drive = 2.5f;
            break;
        case Sfx::HitHeavy:  // remate: impacto, crujido largo y grave que retumba
            click(0, 1.0f);
            thump(0, 160, 38, 0.1f, 1.1f);
            sub(0.01f, 70, 32, 0.45f, 0.8f);
            crunch(0, 0.22f, 900, 1.1f);
            metal(0.004f, 420, 0.09f, 0.4f);
            drive = 3.0f;
            wet = 0.22f;
            break;
        case Sfx::Jump: whoosh(0, 0.12f, 400, 1100, 0.35f, 0); break;
        case Sfx::Dodge: whoosh(0, 0.24f, 500, 2600, 0.8f, 0); break;
        case Sfx::Guard: click(0, 0.8f); metal(0, 520, 0.12f, 0.7f); crunch(0, 0.05f, 2500, 0.5f); drive = 1.8f; break;
        case Sfx::PerfectGuard:  // choque limpio que resuena
            click(0, 1.0f);
            metal(0, 780, 0.35f, 0.8f, -0.2f);
            metal(0.01f, 1170, 0.3f, 0.5f, 0.2f);
            sub(0, 120, 60, 0.2f, 0.5f);
            drive = 1.5f;
            wet = 0.3f;
            break;
        case Sfx::HeroHurt: click(0, 0.6f); thump(0, 120, 45, 0.09f, 1.0f); crunch(0, 0.12f, 500, 0.8f); drive = 2.5f; break;
        case Sfx::EnemyWindup: {  // gruñido grave que sube: aviso del ataque
            size_t n = len(0.4f);
            float ph = 0;
            LowPass lp;
            for (size_t i = 0; i < n; ++i) {
                float tt = float(i) / kSampleRate;
                ph += (70.0f + 90.0f * tt) / kSampleRate;
                float saw = 2.0f * (ph - std::floor(ph)) - 1.0f;
                t.add(i, lp.process(saw + nz.next() * 0.5f, 900.0f) * std::min(1.0f, tt / 0.25f) * 0.5f, 0);
            }
            drive = 2.0f;
            break;
        }
        case Sfx::EnemyDie:  // se deshace: desgarro, grave que cae y un soplo que se apaga
            click(0, 0.7f);
            crunch(0, 0.35f, 600, 1.0f);
            sub(0, 110, 30, 0.6f, 0.7f);
            swell(0.05f, 0.4f, 1800, 0.25f);
            drive = 2.0f;
            wet = 0.35f;
            break;
        case Sfx::Pickup: click(0, 0.3f); fmNote(t, 0, 69, 0.12f, 0, 1.0f, 1.2f, 0.12f, 0.4f); break;
        case Sfx::Coin: metal(0, 1400, 0.05f, 0.25f); break;
        case Sfx::ItemGet:  // acorde grave y abierto, sin campanitas
            fmNote(t, 0, 50, 0.25f, -0.2f, 1.0f, 0.8f, 0.6f, 1.6f);
            fmNote(t, 0.04f, 57, 0.2f, 0.2f, 1.0f, 0.8f, 0.6f, 1.6f);
            fmNote(t, 0.08f, 62, 0.15f, 0, 2.0f, 0.6f, 0.6f, 1.6f);
            wet = 0.4f;
            break;
        case Sfx::Fire: whoosh(0, 0.4f, 250, 1400, 1.0f, 0); crunch(0.05f, 0.3f, 400, 0.4f); drive = 1.5f; break;
        case Sfx::FireBoom:  // explosión: impacto, grave largo y escombros
            click(0, 1.0f);
            thump(0, 120, 30, 0.2f, 1.1f);
            sub(0, 60, 25, 0.9f, 1.0f);
            crunch(0, 0.7f, 500, 1.2f);
            crunch(0.15f, 0.5f, 2200, 0.3f);
            drive = 3.0f;
            wet = 0.3f;
            break;
        case Sfx::Ice:  // golpe de luz: cristal que se quiebra
            click(0, 0.8f);
            metal(0, 1800, 0.08f, 0.5f, -0.3f);
            metal(0.015f, 2300, 0.07f, 0.4f, 0.3f);
            crunch(0, 0.1f, 4000, 0.5f);
            drive = 1.6f;
            break;
        case Sfx::Cure: swell(0, 0.5f, 900, 0.5f); fmNote(t, 0.4f, 57, 0.18f, 0, 1.0f, 0.7f, 0.5f, 1.0f); wet = 0.4f; break;
        case Sfx::Chest:  // cerrojo pesado y un tono grave
            click(0, 0.7f);
            thump(0, 220, 80, 0.04f, 0.6f);
            metal(0.08f, 300, 0.12f, 0.5f);
            click(0.2f, 0.6f);
            fmNote(t, 0.25f, 45, 0.2f, 0, 1.0f, 0.6f, 0.6f, 1.2f);
            drive = 1.5f;
            wet = 0.3f;
            break;
        case Sfx::Crate: click(0, 0.8f); crunch(0, 0.18f, 1200, 1.0f); thump(0, 210, 80, 0.05f, 0.6f); drive = 2.2f; break;
        case Sfx::WaveStart: {  // golpe grave lejano y un zumbido que crece
            sub(0, 50, 40, 1.4f, 0.8f);
            swell(0, 1.1f, 500, 0.6f);
            thump(1.1f, 90, 35, 0.15f, 0.9f);
            drive = 1.8f;
            wet = 0.4f;
            break;
        }
        case Sfx::Echo: {  // acorde menor grave que crece desde la nada
            const int notes[4] = {45, 52, 57, 60};
            for (int k = 0; k < 4; ++k) {
                float f = midiHz(float(notes[k]));
                size_t n = len(1.4f);
                float ph = 0;
                for (size_t i = 0; i < n; ++i) {
                    float tt = float(i) / kSampleRate;
                    ph += f / kSampleRate;
                    float env = tt < 1.0f ? tt * tt : std::exp(-(tt - 1.0f) / 0.12f);
                    t.add(i, std::sin(kTau * ph) * env * 0.12f, -0.6f + 0.4f * float(k));
                }
            }
            swell(0, 1.0f, 1200, 0.15f);
            wet = 0.5f;
            break;
        }
        case Sfx::Blip: click(0, 0.25f); break;
        case Sfx::MenuMove: click(0, 0.3f); break;
        case Sfx::MenuOk: click(0, 0.45f); thump(0, 200, 120, 0.03f, 0.3f); break;
        // --- Vela: aguda (360-480 Hz), limpia, con una sonrisa ---
        case Sfx::VelaAttack1:  // «¡ha!»
            voice(t, nz, 0, {ph(kA, 0.04f, 0, 0.5f), ph(kA, 0.13f)}, {{380, 430, 400, 330}, 0.08f}, 0.5f);
            break;
        case Sfx::VelaAttack2:  // «¡hya!»
            voice(t, nz, 0, {ph(kI, 0.035f, 0, 0.5f), ph(kI, 0.04f), ph(kA, 0.12f)}, {{400, 450, 420, 340}, 0.08f}, 0.5f);
            break;
        case Sfx::VelaFinisher:  // «¡ha-yaaah!»
            voice(t, nz, 0, {ph(kA, 0.04f, 0, 0.5f), ph(kA, 0.08f), ph(kI, 0.05f, 1, 0, 0, 0.7f), ph(kA, 0.3f)},
                  {{370, 400, 480, 380}, 0.07f}, 0.55f);
            wet = 0.12f;
            break;
        case Sfx::VelaJump:  // «¡hup!»
            voice(t, nz, 0, {ph(kU, 0.03f, 0, 0.6f), ph(kU, 0.07f)}, {{420, 450, 460, 440}, 0.1f}, 0.35f);
            break;
        case Sfx::VelaHurt:  // «¡ah!» que cae
            voice(t, nz, 0, {ph(kAh, 0.04f, 0.6f, 0.3f), ph(kA, 0.17f, 1, 0.1f)}, {{500, 470, 360, 290}, 0.2f}, 0.5f);
            break;
        case Sfx::VelaDodge:  // resoplido corto
            voice(t, nz, 0, {ph(kU, 0.07f, 0, 0.8f), ph(kU, 0.04f, 0.3f, 0.3f)}, {{400, 400, 380, 360}, 0.15f}, 0.25f);
            break;
        // --- Hollín: grave (220-300 Hz), ronca y con mala idea ---
        case Sfx::HollinAttack1:  // «¡hah!»
            voice(t, nz, 0, {ph(kAh, 0.05f, 0, 0.6f), ph(kA, 0.16f)}, {{260, 305, 280, 215}, 0.14f, 0.35f, 2.0f, 0.8f}, 0.55f);
            break;
        case Sfx::HollinAttack2:  // «¡rah!»
            voice(t, nz, 0, {ph(kAh, 0.07f, 1, 0.1f, 1), ph(kA, 0.15f)}, {{250, 295, 270, 205}, 0.12f, 0.4f, 2.2f, 0.8f}, 0.55f);
            break;
        case Sfx::HollinFinisher:  // «¡raaagh!»
            voice(t, nz, 0, {ph(kAh, 0.08f, 1, 0.1f, 1), ph(kA, 0.34f), ph(kAh, 0.07f, 0.3f, 0.6f)},
                  {{230, 290, 280, 190}, 0.15f, 0.55f, 3.0f, 0.7f}, 0.6f);
            wet = 0.12f;
            break;
        case Sfx::HollinJump:  // «¡heh!»
            voice(t, nz, 0, {ph(kE, 0.035f, 0, 0.6f), ph(kE, 0.08f)}, {{290, 300, 270, 250}, 0.12f, 0.3f, 1.6f}, 0.4f);
            break;
        case Sfx::HollinHurt:  // «¡tch... gh!»
            click(0, 0.4f);
            voice(t, nz, 0.03f, {ph(kAh, 0.05f, 0.4f, 0.4f), ph(kAh, 0.13f, 1, 0.2f)}, {{250, 230, 190, 160}, 0.15f, 0.65f, 2.4f, 0.7f},
                  0.5f);
            break;
        case Sfx::HollinDodge:  // risita por la nariz
            voice(t, nz, 0, {ph(kE, 0.05f, 0, 0.7f), ph(kE, 0.04f, 0.4f, 0.4f)}, {{290, 300, 280, 260}, 0.15f, 0.3f}, 0.25f);
            break;
        case Sfx::HollinLaugh: {  // «je, je, je»
            const float pitch[3] = {300, 315, 285};
            for (int k = 0; k < 3; ++k)
                voice(t, nz, 0.13f * float(k), {ph(kE, 0.04f, 0, 0.7f), ph(kE, 0.07f, 1, 0, 0, 1.0f - 0.2f * float(k))},
                      {{pitch[k], pitch[k] + 15, pitch[k] - 5, pitch[k] - 30}, 0.12f, 0.35f, 1.6f}, 0.42f - 0.08f * float(k));
            break;
        }
        case Sfx::Abuelo:  // solo grabado (sfxVariants)
        case Sfx::Count: break;
    }
    // Saturación suave (tanh normalizada): golpes con más mordida, sin recortar.
    if (drive > 1.0f) {
        float norm = 1.0f / std::tanh(drive);
        for (size_t i = 0; i < t.size(); ++i) {
            t.l[i] = std::tanh(t.l[i] * drive) * norm * 0.9f;
            t.r[i] = std::tanh(t.r[i] * drive) * norm * 0.9f;
        }
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

bool loadAnyWav(const std::string& path, StereoBuffer& buf) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    std::vector<unsigned char> d;
    unsigned char chunk[65536];
    size_t got;
    while ((got = std::fread(chunk, 1, sizeof(chunk), f)) > 0) d.insert(d.end(), chunk, chunk + got);
    std::fclose(f);
    auto u16 = [&](size_t at) { return uint32_t(d[at]) | uint32_t(d[at + 1]) << 8; };
    auto u32 = [&](size_t at) { return u16(at) | u16(at + 2) << 16; };
    if (d.size() < 12 || std::memcmp(d.data(), "RIFF", 4) != 0 || std::memcmp(d.data() + 8, "WAVE", 4) != 0) return false;
    uint32_t format = 0, channels = 0, rate = 0, bits = 0;
    size_t dataAt = 0, dataLen = 0;
    for (size_t at = 12; at + 8 <= d.size();) {
        uint32_t len = u32(at + 4);
        if (!std::memcmp(d.data() + at, "fmt ", 4) && at + 24 <= d.size()) {
            format = u16(at + 8), channels = u16(at + 10), rate = u32(at + 12), bits = u16(at + 22);
            if (format == 0xFFFE && len >= 26 && at + 34 <= d.size()) format = u16(at + 32);  // extensible: el subformato
        } else if (!std::memcmp(d.data() + at, "data", 4)) {
            dataAt = at + 8;
            dataLen = std::min<size_t>(len, d.size() - dataAt);
        }
        at += 8 + len + (len & 1);
    }
    if (!dataAt || !channels || !rate || !(format == 1 || format == 3)) return false;
    size_t bytes = bits / 8, frames = dataLen / (bytes * channels);
    auto sample = [&](size_t i, uint32_t ch) -> float {
        size_t at = dataAt + (i * channels + ch) * bytes;
        if (format == 3 && bits == 32) {
            float v;
            std::memcpy(&v, d.data() + at, 4);
            return v;
        }
        if (bits == 16) return float(int16_t(u16(at))) / 32768.0f;
        if (bits == 24) return float(int32_t(u32(at - 1) & 0xFFFFFF00u)) / 2147483648.0f;
        if (bits == 32) return float(int32_t(u32(at))) / 2147483648.0f;
        if (bits == 8) return (float(d[at]) - 128.0f) / 128.0f;
        return 0.0f;
    };
    // A estéreo y a kSampleRate (interpolación lineal).
    double step = double(rate) / kSampleRate;
    size_t outFrames = size_t(double(frames) / step);
    buf.data.assign(outFrames * 2, 0);
    for (size_t o = 0; o < outFrames; ++o) {
        double src = double(o) * step;
        size_t k = size_t(src);
        float t = float(src - double(k));
        size_t k1 = std::min(k + 1, frames - 1);
        for (uint32_t ch = 0; ch < 2; ++ch) {
            uint32_t c = std::min(ch, channels - 1);
            float v = sample(k, c) * (1 - t) + sample(k1, c) * t;
            buf.data[o * 2 + ch] = int16_t(std::clamp(v, -1.0f, 1.0f) * 32000.0f);
        }
    }
    return outFrames > 0;
}

namespace {

// Voces: personaje (0 Vela, 1 Hollín) y qué dicen (nombre del fichero).
bool voiceInfo(Sfx id, int& who, const char*& line) {
    switch (id) {
        case Sfx::VelaAttack1: case Sfx::VelaAttack2: who = 0, line = "golpe"; return true;
        case Sfx::VelaFinisher: who = 0, line = "remate"; return true;
        case Sfx::VelaJump: who = 0, line = "salto"; return true;
        case Sfx::VelaHurt: who = 0, line = "dolor"; return true;
        case Sfx::VelaDodge: who = 0, line = "esquiva"; return true;
        case Sfx::HollinAttack1: case Sfx::HollinAttack2: who = 1, line = "golpe"; return true;
        case Sfx::HollinFinisher: who = 1, line = "remate"; return true;
        case Sfx::HollinJump: who = 1, line = "salto"; return true;
        case Sfx::HollinHurt: who = 1, line = "dolor"; return true;
        case Sfx::HollinDodge: who = 1, line = "esquiva"; return true;
        case Sfx::HollinLaugh: who = 1, line = "risa"; return true;
        default: return false;
    }
}

std::vector<StereoBuffer> loadTakes(const std::string& dir, int who, const char* line) {
    std::vector<StereoBuffer> takes;
    std::string base = dir + "/" + (who ? "hollin_" : "vela_") + line;
    StereoBuffer b;
    if (loadAnyWav(base + ".wav", b)) takes.push_back(b);
    for (int n = 1; n <= 12; ++n)
        if (loadAnyWav(base + "_" + std::to_string(n) + ".wav", b)) takes.push_back(b);
    for (StereoBuffer& t : takes) {  // todas al mismo pico (-3 dB), vengan como vengan grabadas
        int peak = 1;
        for (int16_t v : t.data) peak = std::max(peak, std::abs(int(v)));
        float k = 0.7f * 32000.0f / float(peak);
        for (int16_t& v : t.data) v = int16_t(std::clamp(float(v) * k, -32000.0f, 32000.0f));
    }
    return takes;
}

}  // namespace

std::vector<StereoBuffer> sfxVariants(Sfx id, const std::string& voiceDir) {
    if (id == Sfx::Abuelo) {  // grabado o nada; las que falten quedan vacías (se calla en esa)
        std::vector<StereoBuffer> takes(size_t(BarkKind::Count) * kBarkTakes);
        bool any = false;
        for (int k = 0; k < int(BarkKind::Count); ++k)
            for (int n = 0; n < kBarkTakes; ++n)
                any |= !voiceDir.empty() &&
                       loadOgg(voiceDir + "/abuelo/" + kAbueloBarks[k][n].file + ".ogg", takes[size_t(k * kBarkTakes + n)]);
        if (!any) takes.clear();
        return takes;
    }
    int who;
    const char* line;
    if (voiceDir.empty() || !voiceInfo(id, who, line)) return {renderSfx(id)};
    std::vector<StereoBuffer> takes = loadTakes(voiceDir, who, line);
    if (!takes.empty()) return takes;
    // ¿Tiene alguna grabación? Entonces lo que falta sale de la más parecida (o se calla):
    // mezclar su voz con una sintética desentona.
    const char* lines[] = {"golpe", "remate", "salto", "dolor", "esquiva", "risa"};
    bool recorded = false;
    for (const char* l : lines) recorded = recorded || !loadTakes(voiceDir, who, l).empty();
    if (!recorded) return {renderSfx(id)};
    const char* fallback = !std::strcmp(line, "remate") ? "golpe" : !std::strcmp(line, "esquiva") ? "salto" : nullptr;
    if (fallback) return loadTakes(voiceDir, who, fallback);
    return {};
}

// --- Vorbis --------------------------------------------------------------------------------
// libvorbisfile se abre en tiempo de ejecución, como PulseAudio: sin cabeceras al compilar.
// OggVorbis_File es una estructura opaca para nosotros: se le reserva sitio de sobra.
bool loadOgg(const std::string& path, StereoBuffer& buf) {
    static void* lib = dlopen("libvorbisfile.so.3", RTLD_NOW);
    if (!lib) return false;
    struct VorbisInfo {
        int version, channels;
        long rate;
    };
    using OvFopen = int (*)(const char*, void*);
    using OvInfo = VorbisInfo* (*)(void*, int);
    using OvRead = long (*)(void*, char*, int, int, int, int, int*);
    using OvClear = int (*)(void*);
    auto ovFopen = (OvFopen)dlsym(lib, "ov_fopen");
    auto ovInfo = (OvInfo)dlsym(lib, "ov_info");
    auto ovRead = (OvRead)dlsym(lib, "ov_read");
    auto ovClear = (OvClear)dlsym(lib, "ov_clear");
    if (!ovFopen || !ovInfo || !ovRead || !ovClear) return false;
    alignas(16) static thread_local unsigned char vf[8192];
    if (ovFopen(path.c_str(), vf) != 0) return false;
    VorbisInfo* info = ovInfo(vf, -1);
    int channels = info ? info->channels : 0;
    long rate = info ? info->rate : 0;
    std::vector<int16_t> pcm;
    char chunk[16384];
    int section = 0;
    for (long n; (n = ovRead(vf, chunk, sizeof(chunk), 0, 2, 1, &section)) > 0;) {
        size_t at = pcm.size();
        pcm.resize(at + size_t(n) / 2);
        std::memcpy(pcm.data() + at, chunk, size_t(n));
    }
    ovClear(vf);
    if (channels < 1 || channels > 2 || rate != kSampleRate || pcm.empty()) return false;  // los temas van a 44,1 kHz
    buf.data.resize(pcm.size() / size_t(channels) * 2);
    for (size_t f = 0; f < buf.frames(); ++f) {
        buf.data[f * 2] = pcm[f * size_t(channels)];
        buf.data[f * 2 + 1] = pcm[f * size_t(channels) + size_t(channels - 1)];
    }
    return true;
}
