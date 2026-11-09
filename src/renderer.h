#pragma once

// Render "bonito": todo el mundo se dibuja como cajas sólidas instanciadas (un único cubo
// en la GPU + una matriz y un color por instancia) con un shader de iluminación:
//   - sol direccional con sombras (shadow mapping, 2048², PCF por hardware + 4 muestras)
//   - luz ambiente hemisférica (cielo arriba, rebote del suelo abajo)
//   - especular Blinn-Phong, luz de contorno (rim) en personajes, bisel en las aristas
//   - suelo con cuadrícula procedural antialiasada, cielo en degradado, niebla
//   - tonemapping ACES y corrección gamma
//   - efectos: llamas procedurales (ruido fractal animado) y humo, como billboards con
//     alfa premultiplicado; luces puntuales (fuego) y manchas de chamuscado en el suelo
// Coste: 4 draw calls (sombras, cielo, escena, efectos) sea cual sea el número de objetos.

#include "gl.h"
#include "vmath.h"

#include <cstdint>
#include <vector>

enum class Material : int {
    Cloth = 0,  // personajes: difuso + contorno
    Stone = 1,  // plataformas: bisel ancho en las aristas
    Metal = 2,  // armas, escudo: especular fuerte
    Ground = 3, // suelo: cuadrícula procedural
};

struct RenderCamera {
    Mat4 view, proj;
    Vec3 position, forward, right, up;
    float fovY = 1.0f, aspect = 1.0f;
};

class Renderer {
public:
    bool init();
    void shutdown();

    // Caja con semiejes `half` centrada en `center`, en el espacio de `transform`.
    // `flash` (0..1) la blanquea (impactos).
    void box(const Mat4& transform, Vec3 center, Vec3 half, uint32_t color, Material m, float flash = 0);

    // Llama vertical con la base en `base`. `seed` desincroniza su animación.
    void flame(Vec3 base, float width, float height, float seed, float intensity);
    void smoke(Vec3 center, float size, float seed, float opacity);
    // Luz puntual (solo se usan las kMaxLights más intensas del frame).
    void light(Vec3 pos, Vec3 color, float radius);
    // Mancha de chamuscado sobre las superficies horizontales cercanas a `pos`.
    void scorch(Vec3 pos, float radius, float strength);

    static constexpr int kMaxLights = 8, kMaxScorches = 8;

    // Dibuja cielo + sombras + escena en el framebuffer actual y vacía la lista.
    // `shadowCenter`: centro del área con sombras (normalmente el jugador).
    void render(const RenderCamera& cam, Vec3 shadowCenter, int width, int height, float time);

    size_t instanceCount() const { return instances_.size(); }

private:
    struct Instance {
        float model[16];
        uint32_t color;
        float material, flash, pad;
    };

    struct FxInstance {
        float pos[3], seed;
        float width, height, intensity, kind;  // kind: 0 = llama, 1 = humo
    };
    struct PointLight {
        Vec3 pos, color;
        float radius;
    };

    std::vector<Instance> instances_;
    std::vector<FxInstance> smoke_, flames_;
    std::vector<PointLight> lights_;
    std::vector<float> scorches_;  // x, y, z, radio, intensidad por mancha
    GLuint cubeVbo_ = 0, instanceVbo_ = 0, vao_ = 0, emptyVao_ = 0;
    GLuint meshProgram_ = 0, shadowProgram_ = 0, skyProgram_ = 0, fxProgram_ = 0;
    GLuint fxVao_ = 0, fxVbo_ = 0;
    size_t fxCapacity_ = 0;
    GLuint shadowFbo_ = 0, shadowTex_ = 0;
    size_t instanceCapacity_ = 0;
};
