#pragma once

// Aprendizaje por refuerzo de los Grises: Q-learning tabular.
//
// Cada medio segundo, un Gris mira la situación (estado discreto) y elige una táctica
// (acción). Después recibe una recompensa: positiva si te hace daño; negativa si le
// golpeas, si bloqueas su ataque o si lo esquivas. La tabla Q aprende qué táctica rinde
// mejor en cada situación:
//     Q(s,a) ← Q(s,a) + α · (r + γ · max Q(s',·) − Q(s,a))
// y se elige la mejor casi siempre, explorando otra al azar con probabilidad ε.
//
// Cada especie comparte un cerebro (aprenden juntos de tu forma de jugar) que se guarda
// entre partidas. Los jefes tienen el suyo, con α alto: se adaptan durante el combate.

#include <cstdio>
#include <string>

enum class Intent { Approach, Attack, Retreat, Circle, Wait, Feint, Count };
const char* intentName(Intent i);

// Situación vista por un Gris.
struct BrainInput {
    float distance;     // al Prismero (m)
    int heroAction;     // 0 quieto/andando, 1 atacando, 2 en guardia, 3 esquivando o hecho luz, 4 en el aire
    float healthFrac;   // su vida (0..1)
    bool canAttack;     // sin espera de ataque pendiente
};

class Brain {
public:
    static constexpr int kStates = 3 * 5 * 2 * 2;
    static constexpr int kActions = int(Intent::Count);

    float alpha = 0.15f;   // velocidad de aprendizaje
    float gamma = 0.8f;    // peso del futuro
    float epsilon = 0.15f; // exploración

    static int state(const BrainInput& in);
    // `r1`, `r2` ∈ [0,1): números aleatorios (para explorar y elegir al azar).
    Intent choose(int s, float r1, float r2) const;
    void learn(int s, Intent a, float reward, int next, bool terminal);
    float q(int s, Intent a) const { return q_[s][int(a)]; }
    Intent best(int s) const;
    int experience() const { return updates_; }

    bool save(const std::string& path) const;
    bool load(const std::string& path);

private:
    float q_[kStates][kActions] = {};
    int updates_ = 0;
};
