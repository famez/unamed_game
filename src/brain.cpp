#include "brain.h"

#include <algorithm>

const char* intentName(Intent i) {
    switch (i) {
        case Intent::Approach: return "acercarse";
        case Intent::Attack: return "atacar";
        case Intent::Retreat: return "retroceder";
        case Intent::Circle: return "rodear";
        case Intent::Wait: return "esperar";
        case Intent::Feint: return "amagar";
        case Intent::Flank: return "flanquear";
        case Intent::Count: break;
    }
    return "?";
}

int Brain::state(const BrainInput& in) {
    int d = in.distance < 2.2f ? 0 : in.distance < 5.0f ? 1 : 2;
    int h = std::clamp(in.heroAction, 0, 4);
    int hp = in.healthFrac > 0.5f ? 0 : 1;
    int cd = in.canAttack ? 1 : 0;
    return (((d * 5 + h) * 2 + hp) * 2 + cd) * 2 + (in.watched ? 1 : 0);
}

Intent Brain::best(int s) const {
    int b = 0;
    for (int a = 1; a < kActions; ++a)
        if (q_[s][a] > q_[s][b]) b = a;
    return Intent(b);
}

Intent Brain::choose(int s, float r1, float r2) const {
    // Explora menos cuanto más sabe (hasta un tercio de ε con mucha experiencia).
    float eps = epsilon * (0.33f + 0.67f / (1.0f + float(updates_) / 400.0f));
    if (r1 < eps) return Intent(std::min(kActions - 1, int(r2 * kActions)));
    return best(s);
}

void Brain::learn(int s, Intent a, float reward, int next, bool terminal) {
    float target = reward;
    if (!terminal) target += gamma * q_[next][int(best(next))];
    float& v = q_[s][int(a)];
    v += alpha * (target - v);
    ++updates_;
}

// Fichero binario sencillo: cabecera, número de actualizaciones y la tabla. La versión de
// la cabecera cambia con la forma de la tabla (los ficheros viejos se ignoran).
bool Brain::save(const std::string& path) const {
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    const char magic[4] = {'Q', 'G', 'R', '2'};
    bool ok = std::fwrite(magic, 1, 4, f) == 4 && std::fwrite(&updates_, sizeof(updates_), 1, f) == 1 &&
              std::fwrite(q_, sizeof(q_), 1, f) == 1;
    std::fclose(f);
    return ok;
}

bool Brain::load(const std::string& path) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    char magic[4];
    bool ok = std::fread(magic, 1, 4, f) == 4 && magic[0] == 'Q' && magic[1] == 'G' && magic[2] == 'R' && magic[3] == '2' &&
              std::fread(&updates_, sizeof(updates_), 1, f) == 1 && std::fread(q_, sizeof(q_), 1, f) == 1;
    std::fclose(f);
    return ok;
}
