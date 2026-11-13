#pragma once

// Render "bonito": todo el mundo se dibuja como cajas sólidas instanciadas (un único cubo
// en la GPU + una matriz y un color por instancia) con un shader de iluminación:
//   - sol direccional con sombras (shadow mapping, 2048², PCF por hardware + 4 muestras)
//   - luz ambiente hemisférica (cielo arriba, rebote del suelo abajo)
//   - especular Blinn-Phong, luz de contorno (rim) en personajes, bisel en las aristas
//   - suelo con cuadrícula procedural antialiasada, cielo en degradado, niebla
//   - tonemapping ACES y corrección gamma
//   - fuego volumétrico: cajas recorridas por raymarching con ruido fractal 3D animado
//     (textura 3D precalculada; humo y fuego se dibujan a media resolución)
//   - humo en billboards (alfa premultiplicado), luces puntuales y suelo chamuscado
//   - silueta resaltada (stencil buffer) para el objetivo apuntado
// Coste: unos 8 draw calls por frame sea cual sea el número de objetos.

#include "gl.h"
#include "vmath.h"

#include <cstdint>
#include <vector>

// Calidad gráfica: baja (sin MSAA, sombras 1024², bloom corto), media (MSAA 4x, sombras
// 2048²) y alta (MSAA 8x, sombras 4096², bloom más amplio) para equipos más potentes.
enum class Quality { Low, Medium, High };
const char* qualityName(Quality q);

enum class Material : int {
    Cloth = 0,  // personajes: difuso + contorno
    Stone = 1,  // plataformas: bisel ancho en las aristas
    Metal = 2,  // armas, escudo: especular fuerte
    Ground = 3, // suelo de la plaza: adoquines procedurales
    Glow = 4,   // emisivo: no lo oscurecen las sombras (ojos, cristales, orbes, farolas)
    Grass = 5,  // césped con variación de color procedural
    Water = 6,  // agua: ondas animadas y reflejo del cielo
};

// Vértice de una malla deformada por huesos (skinning en la GPU): hasta 2 huesos.
struct SkinVertex {
    float pos[3];
    float normal[3];
    uint32_t color;
    uint8_t joints[4];
    uint8_t weights[4];  // normalizados (suman 255)
    float material;
};

struct RenderCamera {
    Mat4 view, proj;
    Vec3 position, forward, right, up;
    float fovY = 1.0f, aspect = 1.0f;
    float zNear = 0.05f, zFar = 200.0f;
};

class Renderer {
public:
    bool init(Quality q = Quality::Medium);
    void shutdown();
    void setQuality(Quality q);
    Quality quality() const { return quality_; }

    // Fotograma con post-proceso: beginFrame activa el buffer de la escena (todo lo que se
    // dibuje después va ahí, también las capas de líneas y efectos) y endFrame lo resuelve,
    // añade el bloom y lo compone en la pantalla. El canal alfa de la escena es la máscara
    // de brillo: lo emisivo y los efectos escriben alfa y "sangran" luz alrededor.
    // La escena se dibuja a una resolución interna escalada (según la calidad) y se
    // reescala al componerla: la GPU sombrea menos píxeles y el HUD sigue nítido.
    void beginFrame(int width, int height);
    void endFrame();
    int sceneWidth() const { return sceneW_; }
    int sceneHeight() const { return sceneH_; }

    // Caja con semiejes `half` centrada en `center`, en el espacio de `transform`.
    // `flash` (0..1) la blanquea (impactos).
    void box(const Mat4& transform, Vec3 center, Vec3 half, uint32_t color, Material m, float flash = 0);
    // Elipsoide con semiejes (radios) `radii`: para cuerpos redondeados.
    void ellipsoid(const Mat4& transform, Vec3 center, Vec3 radii, uint32_t color, Material m, float flash = 0);

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
    // "El color es energía": croma del mundo (0 gris, 1 a todo color). Afecta solo a lo que
    // se añade entre setScenery(true) y setScenery(false) (el escenario) y al cielo.
    void setWorldColor(float c) { worldColor_ = c; }
    void setScenery(bool on) { scenery_ = on; }
    // Salpicadura de color en el suelo (no le afecta la falta de croma).
    void paint(Vec3 pos, float radius, uint32_t color);
    static constexpr int kMaxPaints = 16;

    static constexpr int kMaxLights = 8, kMaxScorches = 8, kMaxBones = 20;

    // Mallas con skinning (cuerpos continuos de los personajes). Se suben una vez;
    // `skinned` encola un dibujo con las matrices de los huesos (mundo × bind⁻¹).
    // `outline` != 0: contorno de ese color (casco invertido: la malla inflada por detrás).
    int createSkinnedMesh(const std::vector<SkinVertex>& verts, const std::vector<uint32_t>& indices);
    void skinned(int mesh, const Mat4* bones, int boneCount, float flash, uint32_t outline);

    // Dibuja cielo + sombras + escena en el framebuffer actual y vacía la lista.
    // `shadowCenter`: centro del área con sombras (normalmente el jugador).
    void render(const RenderCamera& cam, Vec3 shadowCenter, int width, int height, float time);

    size_t instanceCount() const { return boxes_.items.size() + spheres_.items.size(); }
    size_t skinnedCount() const { return skinDraws_.size(); }

private:
    struct Instance {
        float model[16];
        uint32_t color;
        float material, flash, shape;  // shape: 0 = caja, 1 = elipsoide
    };
    // Instancias de una malla (cubo o esfera) con su propio buffer y VAO: sin
    // glDrawArraysInstancedBaseInstance (GL 4.2), cada lote necesita su buffer.
    struct Batch {
        std::vector<Instance> items;
        GLuint vao = 0, vbo = 0;
        size_t capacity = 0;
        GLint first = 0;
        GLsizei vertexCount = 0;
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

    struct SkinMesh {
        GLuint vao = 0, vbo = 0, ebo = 0;
        GLsizei count = 0;
    };
    struct SkinDraw {
        int mesh;
        float bones[kMaxBones * 16];
        int boneCount;
        float flash;
        uint32_t outline;
    };
    std::vector<SkinMesh> skinMeshes_;
    std::vector<SkinDraw> skinDraws_;
    GLuint skinProgram_ = 0, skinShadowProgram_ = 0;
    void drawSkinned(GLuint prog, bool outlinePass);

    // Post-proceso.
    static constexpr int kBloomLevels = 6;
    Quality quality_ = Quality::Medium;
    int shadowSize_ = 0, msaa_ = 4, bloomLevels_ = 5;
    GLuint targetFbo_ = 0;  // donde dibuja render(): el buffer de la escena
    GLuint sceneFbo_ = 0, sceneColorRb_ = 0, sceneDepthRb_ = 0, resolveFbo_ = 0, resolveTex_ = 0;
    int sceneW_ = 0, sceneH_ = 0, sceneSamples_ = -1;
    int windowW_ = 0, windowH_ = 0;
    float renderScale_ = 0.75f;
    GLuint bloomFbo_[kBloomLevels] = {}, bloomTex_[kBloomLevels] = {};
    int bloomW_[kBloomLevels] = {}, bloomH_[kBloomLevels] = {};
    GLuint brightProgram_ = 0, downProgram_ = 0, upProgram_ = 0, finalProgram_ = 0;
    void ensureSceneTargets(int width, int height);
    void releaseSceneTargets();
    void createShadowMap();

    Batch boxes_, spheres_, highlightBoxes_, highlightSpheres_;
    std::vector<FxInstance> smoke_;
    std::vector<VolumeInstance> volumes_;
    bool highlighting_ = false;
    bool scenery_ = false;
    float worldColor_ = 1.0f;
    std::vector<float> paints_;  // x, y, z, radio, r, g, b
    uint32_t highlightColor_ = 0;
    std::vector<PointLight> lights_;
    std::vector<float> scorches_;  // x, y, z, radio, intensidad por mancha
    GLuint cubeVbo_ = 0, emptyVao_ = 0;  // cubeVbo_: cubo (36 vértices) seguido de la esfera
    GLuint meshProgram_ = 0, shadowProgram_ = 0, skyProgram_ = 0, fxProgram_ = 0;
    GLuint fxVao_ = 0, fxVbo_ = 0;
    GLuint volumeProgram_ = 0, volumeVao_ = 0, volumeVbo_ = 0;
    void setupBatch(Batch& b, GLint first, GLsizei vertexCount);
    void uploadBatch(Batch& b);
    void drawBatch(const Batch& b);
    void addInstance(const Mat4& model, uint32_t color, Material m, float flash, bool sphere);
    void ensureFxTargets(int width, int height);
    void releaseFxTargets();
    bool fxScreenRect(const RenderCamera& cam, int width, int height, int& x0, int& y0, int& x1, int& y1) const;
    GLuint compositeProgram_ = 0, noiseTex_ = 0;
    GLuint depthFbo_ = 0, depthTex_ = 0, fxFbo_ = 0, fxTex_ = 0;
    int fullWidth_ = 0, fullHeight_ = 0, fxWidth_ = 0, fxHeight_ = 0;
    GLuint shadowFbo_ = 0, shadowTex_ = 0;
};
