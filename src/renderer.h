#pragma once

// Render "bonito": todo el mundo se dibuja como cajas sólidas instanciadas (un único cubo
// en la GPU + una matriz y un color por instancia) con un shader de iluminación:
//   - sol direccional con sombras (shadow mapping, 2048², PCF por hardware + 4 muestras)
//   - luz ambiente hemisférica (cielo arriba, rebote del suelo abajo)
//   - especular Blinn-Phong, luz de contorno (rim) en personajes, bisel en las aristas
//   - suelo con cuadrícula procedural antialiasada, cielo en degradado, niebla
//   - tonemapping ACES y corrección gamma
//   - fuego volumétrico: cajas recorridas por raymarching con ruido fractal 3D animado
//   - humo en billboards (alfa premultiplicado), luces puntuales y suelo chamuscado
//   - silueta resaltada (stencil buffer) para el objetivo apuntado
// Coste: unos 8 draw calls por frame sea cual sea el número de objetos.

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

    // Fuego volumétrico en columna: base en `base`, radio y altura. `clipMin/clipMax`
    // recortan el volumen (p. ej. a la cara superior de una plataforma).
    void fireColumn(Vec3 base, float radius, float height, float seed, float intensity,
                    Vec3 clipMin = {-1e4f, -1e4f, -1e4f}, Vec3 clipMax = {1e4f, 1e4f, 1e4f});
    // Bola de fuego esférica (explosión).
    void fireBall(Vec3 center, float radius, float seed, float intensity);

    // Las cajas añadidas entre begin/endHighlight se dibujan con una silueta de `color`
    // alrededor (técnica de stencil: se marcan y luego se pinta su versión inflada fuera).
    void beginHighlight(uint32_t color);
    void endHighlight();
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

    struct FxInstance {  // humo
        float pos[3], seed;
        float size, opacity, pad0, pad1;
    };
    struct VolumeInstance {
        float boxMin[3], seed;
        float boxMax[3], intensity;
        float center[3], radius;
        float height, kind, pad0, pad1;  // kind: 0 = columna, 1 = esfera
    };
    struct PointLight {
        Vec3 pos, color;
        float radius;
    };

    std::vector<Instance> instances_;
    std::vector<FxInstance> smoke_;
    std::vector<VolumeInstance> volumes_;
    std::vector<Instance> highlighted_;
    bool highlighting_ = false;
    uint32_t highlightColor_ = 0;
    std::vector<PointLight> lights_;
    std::vector<float> scorches_;  // x, y, z, radio, intensidad por mancha
    GLuint cubeVbo_ = 0, instanceVbo_ = 0, vao_ = 0, emptyVao_ = 0;
    GLuint meshProgram_ = 0, shadowProgram_ = 0, skyProgram_ = 0, fxProgram_ = 0;
    GLuint fxVao_ = 0, fxVbo_ = 0;
    GLuint volumeProgram_ = 0, volumeVao_ = 0, volumeVbo_ = 0;
    GLuint highlightVao_ = 0, highlightVbo_ = 0;
    void setupMeshVao(GLuint vao, GLuint instanceVbo);
    void drawVolumes(const std::vector<VolumeInstance>& list, bool cameraInside);
    GLuint shadowFbo_ = 0, shadowTex_ = 0;
    size_t instanceCapacity_ = 0;
};
