#pragma once

// Historia: guiones en ficheros de texto (assets/story/*.txt) y el que los reproduce.
// Lógica pura (sin GL): la usan el juego y los tests.
//
// Formato del guion:
//   # comentario
//   == id_de_escena          empieza una escena
//   !aviso                   (opcional) no detiene el juego: subtítulo que pasa solo
//   !recuerdo                (opcional) escena de memoria: pantalla velada, voz lejana
//   hablante: texto          una línea de diálogo (hablante: brizna, prota, voz, sistema...)
//   !orden argumentos        una orden para el juego cuando se llega a ella
//                            (levantarse, nombre <hablante> <Nombre>, oleadas, fin_capitulo...)

#include <string>
#include <vector>

struct StoryLine {
    bool command = false;
    std::string speaker;  // en las órdenes: el nombre de la orden
    std::string text;     // en las órdenes: los argumentos
};

struct Scene {
    std::string id;
    bool blocking = true;  // detiene el juego hasta que se lee
    bool memory = false;   // escena de recuerdo
    std::vector<StoryLine> lines;
};

class Script {
public:
    bool load(const std::string& path);
    bool parse(const std::string& text);
    const Scene* find(const std::string& id) const;
    const std::vector<Scene>& scenes() const { return scenes_; }

private:
    std::vector<Scene> scenes_;
};

// Reproduce una escena línea a línea con efecto de máquina de escribir. Las órdenes se
// entregan al llegar a ellas; las escenas de aviso avanzan solas.
class Dialogue {
public:
    static constexpr float kCharsPerSecond = 42.0f;

    void start(const Scene* scene);
    void stop() { scene_ = nullptr; }
    bool active() const { return scene_ != nullptr; }
    bool blocking() const { return scene_ && scene_->blocking; }
    bool memory() const { return scene_ && scene_->memory; }
    const Scene* scene() const { return scene_; }
    // Avanza el texto; deja en `commands` las órdenes alcanzadas ("nombre args").
    void update(float dt, std::vector<StoryLine>& commands);
    // Pulsación del jugador: si la línea aún se está escribiendo la completa; si no, pasa
    // a la siguiente.
    void advance();
    const StoryLine* line() const;
    // Caracteres (no bytes: UTF-8) visibles de la línea actual y si ya está completa.
    int visibleChars() const { return int(reveal_); }
    bool lineComplete() const;
    // Caracteres que se acaban de revelar en el último update (para el sonido).
    int newChars() const { return newChars_; }

private:
    void skipCommands(std::vector<StoryLine>* commands);

    const Scene* scene_ = nullptr;
    size_t index_ = 0;
    float reveal_ = 0;
    float hold_ = 0;  // avisos: tiempo con la línea completa antes de pasar
    int newChars_ = 0;
};

// Utilidades UTF-8.
int utf8Length(const std::string& s);
std::string utf8Prefix(const std::string& s, int chars);
