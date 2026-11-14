#include "render/renderer.h"

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <string>

namespace {

constexpr float kShadowExtent = 30.0f;  // semilado (m) del área con sombras alrededor del jugador
const Vec3 kSunDir = normalize(Vec3{0.45f, 0.8f, 0.35f});  // hacia el sol

// Fuentes GLSL de render/shaders/, metidas en el binario al compilar (kMeshVs, kMeshFs…).
#include "shaders.inc"

// Ruido de valor 3D periódico (para que la textura se repita sin costuras).
float latticeHash(int x, int y, int z, int period) {
    uint32_t h = uint32_t(x & (period - 1)) * 73856093u ^ uint32_t(y & (period - 1)) * 19349663u ^
                 uint32_t(z & (period - 1)) * 83492791u;
    h ^= h >> 13;
    h *= 0x5bd1e995u;
    h ^= h >> 15;
    return float(h & 0xFFFFu) / 65535.0f;
}

// fbm de 3 octavas en una textura de kNoiseSize³ que cubre 16 celdas de la primera octava.
constexpr int kNoiseSize = 128;
std::vector<uint16_t> makeNoiseVolume() {
    const int n = kNoiseSize;
    std::vector<float> acc(size_t(n) * n * n, 0.0f);
    float amp = 0.5f;
    for (int period = 16; period <= 64; period *= 2, amp *= 0.5f) {
        const int cell = n / period;  // texels por celda
        std::vector<float> lattice(size_t(period) * period * period);
        for (int z = 0; z < period; ++z)
            for (int y = 0; y < period; ++y)
                for (int x = 0; x < period; ++x)
                    lattice[(size_t(z) * period + y) * period + x] = latticeHash(x, y, z, period);
        auto L = [&](int x, int y, int z) {
            x &= period - 1; y &= period - 1; z &= period - 1;
            return lattice[(size_t(z) * period + y) * period + x];
        };
        for (int z = 0; z < n; ++z)
            for (int y = 0; y < n; ++y)
                for (int x = 0; x < n; ++x) {
                    int ix = x / cell, iy = y / cell, iz = z / cell;
                    auto fade = [&](int v) { float f = float(v % cell) / float(cell); return f * f * (3 - 2 * f); };
                    float fx = fade(x), fy = fade(y), fz = fade(z);
                    float v = lerpf(lerpf(lerpf(L(ix, iy, iz), L(ix + 1, iy, iz), fx),
                                          lerpf(L(ix, iy + 1, iz), L(ix + 1, iy + 1, iz), fx), fy),
                                    lerpf(lerpf(L(ix, iy, iz + 1), L(ix + 1, iy, iz + 1), fx),
                                          lerpf(L(ix, iy + 1, iz + 1), L(ix + 1, iy + 1, iz + 1), fx), fy),
                                    fz);
                    acc[(size_t(z) * n + y) * n + x] += amp * v;
                }
    }
    std::vector<uint16_t> out(acc.size());
    for (size_t i = 0; i < acc.size(); ++i) out[i] = uint16_t(clampf(acc[i] / 0.875f, 0, 1) * 65535.0f + 0.5f);
    return out;
}

GLuint compile(GLenum type, const std::string& src) {
    GLuint s = glCreateShader(type);
    const char* p = src.c_str();
    glShaderSource(s, 1, &p, nullptr);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[2048];
        glGetShaderInfoLog(s, sizeof(log), nullptr, log);
        std::fprintf(stderr, "Error compilando shader:\n%s\n", log);
        glDeleteShader(s);
        return 0;
    }
    return s;
}

GLuint link(const char* vs, const char* fs, bool common) {
    std::string header = "#version 330 core\n";
    GLuint v = compile(GL_VERTEX_SHADER, header + (common ? kCommonGlsl : "") + vs);
    GLuint f = compile(GL_FRAGMENT_SHADER, header + (common ? kCommonGlsl : "") + fs);
    if (!v || !f) return 0;
    GLuint p = glCreateProgram();
    glAttachShader(p, v);
    glAttachShader(p, f);
    glLinkProgram(p);
    glDeleteShader(v);
    glDeleteShader(f);
    GLint ok = 0;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[2048];
        glGetProgramInfoLog(p, sizeof(log), nullptr, log);
        std::fprintf(stderr, "Error enlazando programa:\n%s\n", log);
        glDeleteProgram(p);
        return 0;
    }
    return p;
}

void setVec3(GLuint prog, const char* name, Vec3 v) { glUniform3f(glGetUniformLocation(prog, name), v.x, v.y, v.z); }
void setMat4(GLuint prog, const char* name, const Mat4& m) {
    glUniformMatrix4fv(glGetUniformLocation(prog, name), 1, GL_FALSE, m.m);
}

struct CubeVertex {
    float p[3], n[3];
};

}  // namespace

bool Renderer::init(Quality q) {
    meshProgram_ = link(kMeshVs, kMeshFs, true);
    skinProgram_ = link(kSkinVs, kMeshFs, true);
    skinShadowProgram_ = link(kSkinShadowVs, kShadowFs, false);
    if (!skinProgram_ || !skinShadowProgram_) return false;
    shadowProgram_ = link(kShadowVs, kShadowFs, false);
    skyProgram_ = link(kSkyVs, kSkyFs, true);
    fxProgram_ = link(kSmokeVs, kSmokeFs, true);
    volumeProgram_ = link(kVolumeVs, kVolumeFs, true);
    compositeProgram_ = link(kCompositeVs, kCompositeFs, false);
    brightProgram_ = link(kCompositeVs, kBrightFs, false);
    downProgram_ = link(kCompositeVs, kDownFs, false);
    upProgram_ = link(kCompositeVs, kUpFs, false);
    finalProgram_ = link(kCompositeVs, kFinalFs, false);
    if (!brightProgram_ || !downProgram_ || !upProgram_ || !finalProgram_) return false;
    if (!meshProgram_ || !shadowProgram_ || !skyProgram_ || !fxProgram_ || !volumeProgram_ || !compositeProgram_)
        return false;

    // Textura 3D de ruido (R16, 4 MB), generada una vez al arrancar.
    std::vector<uint16_t> noise = makeNoiseVolume();
    glGenTextures(1, &noiseTex_);
    glBindTexture(GL_TEXTURE_3D, noiseTex_);
    glTexImage3D(GL_TEXTURE_3D, 0, GL_R16, kNoiseSize, kNoiseSize, kNoiseSize, 0, GL_RED, GL_UNSIGNED_SHORT,
                 noise.data());
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    for (GLenum wrap : {GL_TEXTURE_WRAP_S, GL_TEXTURE_WRAP_T, GL_TEXTURE_WRAP_R})
        glTexParameteri(GL_TEXTURE_3D, wrap, GL_REPEAT);

    // Cubo unidad (-0.5..0.5) con normales por cara. Para cada cara, u × v = normal, así
    // los triángulos quedan en sentido antihorario vistos desde fuera (para el culling).
    std::vector<CubeVertex> verts;
    const Vec3 faces[6][3] = {
        {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}},  {{-1, 0, 0}, {0, 0, 1}, {0, 1, 0}},
        {{0, 1, 0}, {0, 0, 1}, {1, 0, 0}},  {{0, -1, 0}, {1, 0, 0}, {0, 0, 1}},
        {{0, 0, 1}, {1, 0, 0}, {0, 1, 0}},  {{0, 0, -1}, {0, 1, 0}, {1, 0, 0}},
    };
    const float corners[6][2] = {{-1, -1}, {1, -1}, {1, 1}, {-1, -1}, {1, 1}, {-1, 1}};
    for (const auto& f : faces)
        for (const auto& c : corners) {
            Vec3 p = (f[0] + f[1] * c[0] + f[2] * c[1]) * 0.5f;
            verts.push_back({{p.x, p.y, p.z}, {f[0].x, f[0].y, f[0].z}});
        }

    // Esfera de radio 0,5 (mismo tamaño que el cubo unidad: la escala de la instancia da
    // directamente los ejes del elipsoide). 12 meridianos x 8 paralelos = 168 triángulos,
    // con normales suaves (la del punto de la esfera).
    const GLint sphereFirst = GLint(verts.size());
    const int kSlices = 12, kStacks = 8;
    auto spherePoint = [&](int stack, int slice) {
        float theta = kPi * float(stack) / float(kStacks), phi = 2.0f * kPi * float(slice) / float(kSlices);
        Vec3 n{std::sin(theta) * std::cos(phi), std::cos(theta), std::sin(theta) * std::sin(phi)};
        return CubeVertex{{n.x * 0.5f, n.y * 0.5f, n.z * 0.5f}, {n.x, n.y, n.z}};
    };
    for (int st = 0; st < kStacks; ++st)
        for (int sl = 0; sl < kSlices; ++sl) {
            CubeVertex a = spherePoint(st, sl), b = spherePoint(st + 1, sl);
            CubeVertex c = spherePoint(st + 1, sl + 1), d = spherePoint(st, sl + 1);
            // Antihorario visto desde fuera; en los polos uno de los triángulos degenera.
            if (st > 0) verts.insert(verts.end(), {a, d, c});
            if (st < kStacks - 1) verts.insert(verts.end(), {a, c, b});
        }
    const GLsizei sphereCount = GLsizei(verts.size()) - sphereFirst;

    glGenVertexArrays(1, &emptyVao_);
    glGenBuffers(1, &cubeVbo_);
    glBindBuffer(GL_ARRAY_BUFFER, cubeVbo_);
    glBufferData(GL_ARRAY_BUFFER, verts.size() * sizeof(CubeVertex), verts.data(), GL_STATIC_DRAW);
    setupBatch(boxes_, 0, 36);
    setupBatch(highlightBoxes_, 0, 36);
    setupBatch(spheres_, sphereFirst, sphereCount);
    setupBatch(highlightSpheres_, sphereFirst, sphereCount);

    glGenVertexArrays(1, &fxVao_);
    glGenBuffers(1, &fxVbo_);
    glBindVertexArray(fxVao_);
    glBindBuffer(GL_ARRAY_BUFFER, fxVbo_);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, sizeof(FxInstance), (void*)offsetof(FxInstance, pos));
    glVertexAttribDivisor(0, 1);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(FxInstance), (void*)offsetof(FxInstance, size));
    glVertexAttribDivisor(1, 1);

    // Volúmenes de fuego: el mismo cubo + 4 vec4 por instancia.
    glGenVertexArrays(1, &volumeVao_);
    glGenBuffers(1, &volumeVbo_);
    glBindVertexArray(volumeVao_);
    glBindBuffer(GL_ARRAY_BUFFER, cubeVbo_);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(CubeVertex), (void*)offsetof(CubeVertex, p));
    glBindBuffer(GL_ARRAY_BUFFER, volumeVbo_);
    for (int i = 0; i < 4; ++i) {
        glEnableVertexAttribArray(1 + i);
        glVertexAttribPointer(1 + i, 4, GL_FLOAT, GL_FALSE, sizeof(VolumeInstance), (void*)(i * 4 * sizeof(float)));
        glVertexAttribDivisor(1 + i, 1);
    }
    glBindVertexArray(0);

    setQuality(q);
    if (!shadowTex_) return false;
    boxes_.items.reserve(512);
    spheres_.items.reserve(512);
    return true;
}

void Renderer::shutdown() {
    glDeleteFramebuffers(1, &shadowFbo_);
    glDeleteTextures(1, &shadowTex_);
    glDeleteBuffers(1, &cubeVbo_);
    for (Batch* b : {&boxes_, &spheres_, &highlightBoxes_, &highlightSpheres_}) {
        glDeleteBuffers(1, &b->vbo);
        glDeleteVertexArrays(1, &b->vao);
    }
    glDeleteVertexArrays(1, &emptyVao_);
    glDeleteProgram(meshProgram_);
    glDeleteProgram(brightProgram_);
    glDeleteProgram(downProgram_);
    glDeleteProgram(upProgram_);
    glDeleteProgram(finalProgram_);
    releaseSceneTargets();
    glDeleteProgram(skinProgram_);
    glDeleteProgram(skinShadowProgram_);
    for (SkinMesh& m : skinMeshes_) {
        glDeleteBuffers(1, &m.vbo);
        glDeleteBuffers(1, &m.ebo);
        glDeleteVertexArrays(1, &m.vao);
    }
    glDeleteProgram(shadowProgram_);
    glDeleteProgram(skyProgram_);
    glDeleteProgram(fxProgram_);
    glDeleteBuffers(1, &fxVbo_);
    glDeleteVertexArrays(1, &fxVao_);
    glDeleteProgram(volumeProgram_);
    glDeleteBuffers(1, &volumeVbo_);
    glDeleteVertexArrays(1, &volumeVao_);
    glDeleteProgram(compositeProgram_);
    glDeleteTextures(1, &noiseTex_);
    releaseFxTargets();
}

// Proyecta las cajas de humo y fuego y devuelve el rectángulo de píxeles que ocupan.
// Falso si no se ve ninguna. Si alguna cruza el plano de la cámara, toda la pantalla.
bool Renderer::fxScreenRect(const RenderCamera& cam, int width, int height, int& x0, int& y0, int& x1,
                            int& y1) const {
    Mat4 vp = cam.proj * cam.view;
    float minX = 1e9f, minY = 1e9f, maxX = -1e9f, maxY = -1e9f;
    bool full = false;
    auto addBox = [&](Vec3 lo, Vec3 hi) {
        for (int i = 0; i < 8 && !full; ++i) {
            Vec3 p{i & 1 ? hi.x : lo.x, i & 2 ? hi.y : lo.y, i & 4 ? hi.z : lo.z};
            float cx = vp.m[0] * p.x + vp.m[4] * p.y + vp.m[8] * p.z + vp.m[12];
            float cy = vp.m[1] * p.x + vp.m[5] * p.y + vp.m[9] * p.z + vp.m[13];
            float cw = vp.m[3] * p.x + vp.m[7] * p.y + vp.m[11] * p.z + vp.m[15];
            if (cw < cam.zNear) { full = true; break; }
            minX = std::min(minX, cx / cw); maxX = std::max(maxX, cx / cw);
            minY = std::min(minY, cy / cw); maxY = std::max(maxY, cy / cw);
        }
    };
    for (const VolumeInstance& v : volumes_)
        addBox({v.boxMin[0], v.boxMin[1], v.boxMin[2]}, {v.boxMax[0], v.boxMax[1], v.boxMax[2]});
    for (const FxInstance& f : smoke_) {
        float r = f.size * 0.5f;
        addBox(Vec3{f.pos[0] - r, f.pos[1] - r, f.pos[2] - r}, Vec3{f.pos[0] + r, f.pos[1] + r, f.pos[2] + r});
    }
    if (full) { minX = minY = -1; maxX = maxY = 1; }
    minX = std::max(minX, -1.0f); minY = std::max(minY, -1.0f);
    maxX = std::min(maxX, 1.0f); maxY = std::min(maxY, 1.0f);
    if (minX >= maxX || minY >= maxY) return false;
    // A píxeles, redondeando a pares (el buffer de efectos tiene la mitad) y con margen.
    x0 = std::max(0, (int(std::floor((minX * 0.5f + 0.5f) * float(width))) - 2) & ~1);
    y0 = std::max(0, (int(std::floor((minY * 0.5f + 0.5f) * float(height))) - 2) & ~1);
    x1 = std::min(width, int(std::ceil((maxX * 0.5f + 0.5f) * float(width))) + 2);
    y1 = std::min(height, int(std::ceil((maxY * 0.5f + 0.5f) * float(height))) + 2);
    return x1 > x0 && y1 > y0;
}

void Renderer::releaseFxTargets() {
    glDeleteFramebuffers(1, &depthFbo_);
    glDeleteTextures(1, &depthTex_);
    glDeleteFramebuffers(1, &fxFbo_);
    glDeleteTextures(1, &fxTex_);
    depthFbo_ = depthTex_ = fxFbo_ = fxTex_ = 0;
    fxWidth_ = fxHeight_ = 0;
}

// Copia de la profundidad de la escena (resolución completa, sin MSAA) y buffer de color
// a media resolución para los efectos. Se recrean si cambia el tamaño de la ventana.
void Renderer::ensureFxTargets(int width, int height) {
    if (width == fullWidth_ && height == fullHeight_ && fxFbo_) return;
    releaseFxTargets();
    fullWidth_ = width;
    fullHeight_ = height;
    fxWidth_ = std::max(1, (width + 1) / 2);
    fxHeight_ = std::max(1, (height + 1) / 2);

    glGenTextures(1, &depthTex_);
    glBindTexture(GL_TEXTURE_2D, depthTex_);
    // Mismo formato que el framebuffer de la ventana: lo exige glBlitFramebuffer.
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH24_STENCIL8, width, height, 0, GL_DEPTH_STENCIL, GL_UNSIGNED_INT_24_8,
                 nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glGenFramebuffers(1, &depthFbo_);
    glBindFramebuffer(GL_FRAMEBUFFER, depthFbo_);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_TEXTURE_2D, depthTex_, 0);
    glDrawBuffer(GL_NONE);
    glReadBuffer(GL_NONE);

    glGenTextures(1, &fxTex_);
    glBindTexture(GL_TEXTURE_2D, fxTex_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, fxWidth_, fxHeight_, 0, GL_RGBA, GL_HALF_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glGenFramebuffers(1, &fxFbo_);
    glBindFramebuffer(GL_FRAMEBUFFER, fxFbo_);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, fxTex_, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        std::fprintf(stderr, "El framebuffer de efectos no está completo\n");
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void Renderer::setupBatch(Batch& b, GLint first, GLsizei vertexCount) {
    b.first = first;
    b.vertexCount = vertexCount;
    glGenVertexArrays(1, &b.vao);
    glGenBuffers(1, &b.vbo);
    glBindVertexArray(b.vao);
    glBindBuffer(GL_ARRAY_BUFFER, cubeVbo_);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(CubeVertex), (void*)offsetof(CubeVertex, p));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(CubeVertex), (void*)offsetof(CubeVertex, n));
    // Atributos por instancia (divisor 1): matriz (4 columnas), color y parámetros.
    glBindBuffer(GL_ARRAY_BUFFER, b.vbo);
    for (int i = 0; i < 4; ++i) {
        glEnableVertexAttribArray(2 + i);
        glVertexAttribPointer(2 + i, 4, GL_FLOAT, GL_FALSE, sizeof(Instance),
                              (void*)(offsetof(Instance, model) + i * 4 * sizeof(float)));
        glVertexAttribDivisor(2 + i, 1);
    }
    glEnableVertexAttribArray(6);
    glVertexAttribPointer(6, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(Instance), (void*)offsetof(Instance, color));
    glVertexAttribDivisor(6, 1);
    glEnableVertexAttribArray(7);
    glVertexAttribPointer(7, 3, GL_FLOAT, GL_FALSE, sizeof(Instance), (void*)offsetof(Instance, material));
    glVertexAttribDivisor(7, 1);
    glBindVertexArray(0);
}

// Sube las instancias del frame. El buffer crece al doble cuando se queda corto y se
// "huérfana" cada frame (glBufferData con nullptr) para no esperar a la GPU.
void Renderer::uploadBatch(Batch& b) {
    if (b.items.empty()) return;
    glBindBuffer(GL_ARRAY_BUFFER, b.vbo);
    if (b.items.size() > b.capacity) b.capacity = b.items.size() * 2;
    glBufferData(GL_ARRAY_BUFFER, GLsizeiptr(b.capacity * sizeof(Instance)), nullptr, GL_STREAM_DRAW);
    glBufferSubData(GL_ARRAY_BUFFER, 0, GLsizeiptr(b.items.size() * sizeof(Instance)), b.items.data());
}

void Renderer::drawBatch(const Batch& b) {
    if (b.items.empty()) return;
    glBindVertexArray(b.vao);
    glDrawArraysInstanced(GL_TRIANGLES, b.first, b.vertexCount, GLsizei(b.items.size()));
}

void Renderer::fireColumn(Vec3 base, float radius, float height, float seed, float intensity, Vec3 clipMin,
                          Vec3 clipMax) {
    if (radius <= 0 || height <= 0 || intensity <= 0) return;
    VolumeInstance v{};
    Vec3 lo{std::max(base.x - radius, clipMin.x), std::max(base.y, clipMin.y), std::max(base.z - radius, clipMin.z)};
    Vec3 hi{std::min(base.x + radius, clipMax.x), std::min(base.y + height, clipMax.y), std::min(base.z + radius, clipMax.z)};
    if (lo.x >= hi.x || lo.y >= hi.y || lo.z >= hi.z) return;
    v.boxMin[0] = lo.x; v.boxMin[1] = lo.y; v.boxMin[2] = lo.z; v.seed = seed;
    v.boxMax[0] = hi.x; v.boxMax[1] = hi.y; v.boxMax[2] = hi.z; v.intensity = intensity;
    v.center[0] = base.x; v.center[1] = base.y; v.center[2] = base.z; v.radius = radius;
    v.height = height;
    v.kind = 0;
    volumes_.push_back(v);
}

void Renderer::fireBall(Vec3 center, float radius, float seed, float intensity) {
    if (radius <= 0 || intensity <= 0) return;
    VolumeInstance v{};
    v.boxMin[0] = center.x - radius; v.boxMin[1] = center.y - radius; v.boxMin[2] = center.z - radius;
    v.boxMax[0] = center.x + radius; v.boxMax[1] = center.y + radius; v.boxMax[2] = center.z + radius;
    v.seed = seed;
    v.intensity = intensity;
    v.center[0] = center.x; v.center[1] = center.y; v.center[2] = center.z; v.radius = radius;
    v.height = radius;
    v.kind = 1;
    volumes_.push_back(v);
}

void Renderer::beginHighlight(uint32_t color) {
    highlighting_ = true;
    highlightColor_ = color;
}

void Renderer::endHighlight() { highlighting_ = false; }

void Renderer::smoke(Vec3 center, float size, float seed, float opacity) {
    smoke_.push_back({{center.x, center.y, center.z}, seed, size, opacity, 0, 0});
}

void Renderer::light(Vec3 pos, Vec3 color, float radius) {
    // Las luces del escenario también pierden color sin croma.
    if (scenery_) {
        float l = color.x * 0.299f + color.y * 0.587f + color.z * 0.114f;
        float k = 0.15f + 0.85f * worldColor_;
        color = Vec3{l, l, l} + (color - Vec3{l, l, l}) * k;
        color = color * (0.5f + 0.5f * worldColor_);
    }
    lights_.push_back({pos, color, radius});
}

void Renderer::litArea(Vec3 pos, float radius) {
    if (int(litAreas_.size() / 4) >= kMaxLitAreas || radius <= 0) return;
    litAreas_.insert(litAreas_.end(), {pos.x, pos.y, pos.z, radius});
}

void Renderer::darkArea(Vec3 pos, float radius) {
    if (int(darkAreas_.size() / 4) >= kMaxDarkAreas || radius <= 0) return;
    darkAreas_.insert(darkAreas_.end(), {pos.x, pos.y, pos.z, radius});
}

void Renderer::colorZone(Vec3 pos, float radius) {
    if (zones_.size() / 4 >= 8 || radius <= 0) return;
    zones_.insert(zones_.end(), {pos.x, pos.y, pos.z, radius});
}

void Renderer::paint(Vec3 pos, float radius, uint32_t color) {
    if (int(paints_.size() / 7) >= kMaxPaints) return;
    paints_.insert(paints_.end(), {pos.x, pos.y, pos.z, radius, float(color & 0xFF) / 255.0f,
                                   float((color >> 8) & 0xFF) / 255.0f, float((color >> 16) & 0xFF) / 255.0f});
}

void Renderer::scorch(Vec3 pos, float radius, float strength) {
    if (int(scorches_.size() / 5) >= kMaxScorches) return;
    scorches_.insert(scorches_.end(), {pos.x, pos.y, pos.z, radius, strength});
}

void Renderer::addInstance(const Mat4& model, uint32_t color, Material m, float flash, bool sphere) {
    Instance inst;
    for (int i = 0; i < 16; ++i) inst.model[i] = model.m[i];
    inst.color = color;
    inst.material = float(int(m)) + (scenery_ ? 16.0f : 0.0f);
    inst.flash = flash;
    inst.shape = sphere ? 1.0f : 0.0f;
    Batch& b = sphere ? (highlighting_ ? highlightSpheres_ : spheres_) : (highlighting_ ? highlightBoxes_ : boxes_);
    b.items.push_back(inst);
}

void Renderer::box(const Mat4& transform, Vec3 center, Vec3 half, uint32_t color, Material m, float flash) {
    addInstance(transform * translate(center) * scale(half * 2.0f), color, m, flash, false);
}

void Renderer::ellipsoid(const Mat4& transform, Vec3 center, Vec3 radii, uint32_t color, Material m, float flash) {
    addInstance(transform * translate(center) * scale(radii * 2.0f), color, m, flash, true);
}

const char* qualityName(Quality q) {
    switch (q) {
        case Quality::Low: return "BAJA";
        case Quality::Medium: return "MEDIA";
        case Quality::High: return "ALTA";
    }
    return "?";
}

void Renderer::setQuality(Quality q) {
    quality_ = q;
    int shadow = q == Quality::Low ? 1024 : q == Quality::Medium ? 2048 : 4096;
    msaa_ = q == Quality::Low ? 0 : q == Quality::Medium ? 4 : 8;
    bloomLevels_ = q == Quality::Low ? 4 : q == Quality::Medium ? 5 : 6;
    renderScale_ = q == Quality::Low ? 0.6f : q == Quality::Medium ? 0.7f : 1.0f;
    if (shadow != shadowSize_ || !shadowTex_) {
        shadowSize_ = shadow;
        createShadowMap();
    }
    sceneSamples_ = -1;  // se recrean en el siguiente beginFrame
}

// Mapa de sombras: textura de profundidad con comparación por hardware.
void Renderer::createShadowMap() {
    glDeleteFramebuffers(1, &shadowFbo_);
    glDeleteTextures(1, &shadowTex_);
    glGenTextures(1, &shadowTex_);
    glBindTexture(GL_TEXTURE_2D, shadowTex_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, shadowSize_, shadowSize_, 0, GL_DEPTH_COMPONENT,
                 GL_UNSIGNED_INT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
    const float border[4] = {1, 1, 1, 1};  // fuera del mapa: iluminado
    glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, border);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
    glGenFramebuffers(1, &shadowFbo_);
    glBindFramebuffer(GL_FRAMEBUFFER, shadowFbo_);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, shadowTex_, 0);
    glDrawBuffer(GL_NONE);
    glReadBuffer(GL_NONE);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        std::fprintf(stderr, "El framebuffer de sombras no está completo\n");
        glDeleteTextures(1, &shadowTex_);
        shadowTex_ = 0;
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void Renderer::releaseSceneTargets() {
    glDeleteFramebuffers(1, &sceneFbo_);
    glDeleteRenderbuffers(1, &sceneColorRb_);
    glDeleteRenderbuffers(1, &sceneDepthRb_);
    glDeleteFramebuffers(1, &resolveFbo_);
    glDeleteTextures(1, &resolveTex_);
    glDeleteFramebuffers(kBloomLevels, bloomFbo_);
    glDeleteTextures(kBloomLevels, bloomTex_);
    sceneFbo_ = sceneColorRb_ = sceneDepthRb_ = resolveFbo_ = resolveTex_ = 0;
    for (int i = 0; i < kBloomLevels; ++i) bloomFbo_[i] = bloomTex_[i] = 0;
}

// Buffer de la escena (con MSAA según la calidad), su versión resuelta en textura y la
// cadena de texturas del bloom (media resolución, un cuarto...). Se recrean si cambia el
// tamaño de la ventana o la calidad.
void Renderer::ensureSceneTargets(int width, int height) {
    if (width == sceneW_ && height == sceneH_ && sceneSamples_ == msaa_ && sceneFbo_) return;
    releaseSceneTargets();
    sceneW_ = width;
    sceneH_ = height;
    sceneSamples_ = msaa_;
    glGenRenderbuffers(1, &sceneColorRb_);
    glBindRenderbuffer(GL_RENDERBUFFER, sceneColorRb_);
    glRenderbufferStorageMultisample(GL_RENDERBUFFER, msaa_, GL_RGBA8, width, height);
    glGenRenderbuffers(1, &sceneDepthRb_);
    glBindRenderbuffer(GL_RENDERBUFFER, sceneDepthRb_);
    glRenderbufferStorageMultisample(GL_RENDERBUFFER, msaa_, GL_DEPTH24_STENCIL8, width, height);
    glGenFramebuffers(1, &sceneFbo_);
    glBindFramebuffer(GL_FRAMEBUFFER, sceneFbo_);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, sceneColorRb_);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, sceneDepthRb_);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        std::fprintf(stderr, "El framebuffer de la escena no está completo\n");
    auto makeTex = [](GLuint& tex, GLenum fmt, int w, int h) {
        glGenTextures(1, &tex);
        glBindTexture(GL_TEXTURE_2D, tex);
        glTexImage2D(GL_TEXTURE_2D, 0, fmt, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    };
    makeTex(resolveTex_, GL_RGBA8, width, height);
    glGenFramebuffers(1, &resolveFbo_);
    glBindFramebuffer(GL_FRAMEBUFFER, resolveFbo_);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, resolveTex_, 0);
    int w = width, h = height;
    for (int i = 0; i < kBloomLevels; ++i) {
        w = std::max(1, w / 2);
        h = std::max(1, h / 2);
        bloomW_[i] = w;
        bloomH_[i] = h;
        makeTex(bloomTex_[i], GL_R11F_G11F_B10F, w, h);
        glGenFramebuffers(1, &bloomFbo_[i]);
        glBindFramebuffer(GL_FRAMEBUFFER, bloomFbo_[i]);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, bloomTex_[i], 0);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void Renderer::beginFrame(int windowWidth, int windowHeight) {
    windowW_ = windowWidth;
    windowH_ = windowHeight;
    int width = std::max(1, int(float(windowWidth) * renderScale_ + 0.5f));
    int height = std::max(1, int(float(windowHeight) * renderScale_ + 0.5f));
    ensureSceneTargets(width, height);
    targetFbo_ = sceneFbo_;
    glBindFramebuffer(GL_FRAMEBUFFER, sceneFbo_);
    glViewport(0, 0, width, height);
    if (msaa_ > 0)
        glEnable(GL_MULTISAMPLE);
    else
        glDisable(GL_MULTISAMPLE);
    glClearColor(0, 0, 0, 0);  // alfa 0: nada brilla salvo lo que lo escriba
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
}

void Renderer::endFrame() {
    // Resolver el MSAA a textura.
    glBindFramebuffer(GL_READ_FRAMEBUFFER, sceneFbo_);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, resolveFbo_);
    glBlitFramebuffer(0, 0, sceneW_, sceneH_, 0, 0, sceneW_, sceneH_, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);
    glBindVertexArray(emptyVao_);
    glActiveTexture(GL_TEXTURE0);
    auto pass = [&](GLuint prog, GLuint fbo, int w, int h, GLuint src, float srcW, float srcH) {
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glViewport(0, 0, w, h);
        glUseProgram(prog);
        glBindTexture(GL_TEXTURE_2D, src);
        glUniform1i(glGetUniformLocation(prog, "uSrc"), 0);
        glUniform2f(glGetUniformLocation(prog, "uTexel"), 1.0f / srcW, 1.0f / srcH);
        glDrawArrays(GL_TRIANGLES, 0, 3);
    };
    int levels = bloomLevels_;
    pass(brightProgram_, bloomFbo_[0], bloomW_[0], bloomH_[0], resolveTex_, float(sceneW_), float(sceneH_));
    for (int i = 1; i < levels; ++i)
        pass(downProgram_, bloomFbo_[i], bloomW_[i], bloomH_[i], bloomTex_[i - 1], float(bloomW_[i - 1]),
             float(bloomH_[i - 1]));
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE);
    for (int i = levels - 1; i > 0; --i)
        pass(upProgram_, bloomFbo_[i - 1], bloomW_[i - 1], bloomH_[i - 1], bloomTex_[i], float(bloomW_[i]),
             float(bloomH_[i]));
    glDisable(GL_BLEND);

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, windowW_, windowH_);
    glUseProgram(finalProgram_);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, resolveTex_);
    glUniform1i(glGetUniformLocation(finalProgram_, "uScene"), 0);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, bloomTex_[0]);
    glUniform1i(glGetUniformLocation(finalProgram_, "uBloom"), 1);
    glUniform1f(glGetUniformLocation(finalProgram_, "uBloomStrength"), 0.75f);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glActiveTexture(GL_TEXTURE0);
    glBindVertexArray(0);
    targetFbo_ = 0;
}

int Renderer::createSkinnedMesh(const std::vector<SkinVertex>& verts, const std::vector<uint32_t>& indices) {
    SkinMesh m;
    glGenVertexArrays(1, &m.vao);
    glGenBuffers(1, &m.vbo);
    glGenBuffers(1, &m.ebo);
    glBindVertexArray(m.vao);
    glBindBuffer(GL_ARRAY_BUFFER, m.vbo);
    glBufferData(GL_ARRAY_BUFFER, GLsizeiptr(verts.size() * sizeof(SkinVertex)), verts.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m.ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, GLsizeiptr(indices.size() * sizeof(uint32_t)), indices.data(), GL_STATIC_DRAW);
    const GLsizei st = sizeof(SkinVertex);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, st, (void*)offsetof(SkinVertex, pos));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, st, (void*)offsetof(SkinVertex, normal));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, st, (void*)offsetof(SkinVertex, color));
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 4, GL_UNSIGNED_BYTE, GL_FALSE, st, (void*)offsetof(SkinVertex, joints));
    glEnableVertexAttribArray(4);
    glVertexAttribPointer(4, 4, GL_UNSIGNED_BYTE, GL_TRUE, st, (void*)offsetof(SkinVertex, weights));
    glEnableVertexAttribArray(5);
    glVertexAttribPointer(5, 1, GL_FLOAT, GL_FALSE, st, (void*)offsetof(SkinVertex, material));
    glBindVertexArray(0);
    m.count = GLsizei(indices.size());
    skinMeshes_.push_back(m);
    return int(skinMeshes_.size()) - 1;
}

void Renderer::skinned(int mesh, const Mat4* bones, int boneCount, float flash, uint32_t outline) {
    if (mesh < 0 || mesh >= int(skinMeshes_.size())) return;
    SkinDraw d;
    d.mesh = mesh;
    d.boneCount = std::min(boneCount, kMaxBones);
    for (int i = 0; i < d.boneCount; ++i)
        for (int k = 0; k < 16; ++k) d.bones[i * 16 + k] = bones[i].m[k];
    d.flash = flash;
    d.outline = outline;
    skinDraws_.push_back(d);
}

// Dibuja las mallas encoladas con el programa ya activo y sus uniforms de escena puestos.
// En la pasada de contorno: solo las que lo llevan, infladas y con color plano.
void Renderer::drawSkinned(GLuint prog, bool outlinePass) {
    GLint bonesLoc = glGetUniformLocation(prog, "uBones");
    GLint flashLoc = glGetUniformLocation(prog, "uFlash");
    GLint outlineLoc = glGetUniformLocation(prog, "uOutlineColor");
    for (const SkinDraw& d : skinDraws_) {
        if (outlinePass && !d.outline) continue;
        glUniformMatrix4fv(bonesLoc, d.boneCount, GL_FALSE, d.bones);
        if (flashLoc >= 0) glUniform1f(flashLoc, d.flash);
        if (outlinePass)
            glUniform4f(outlineLoc, float(d.outline & 0xFF) / 255.0f, float((d.outline >> 8) & 0xFF) / 255.0f,
                        float((d.outline >> 16) & 0xFF) / 255.0f, 1.0f);
        const SkinMesh& m = skinMeshes_[size_t(d.mesh)];
        glBindVertexArray(m.vao);
        glDrawElements(GL_TRIANGLES, m.count, GL_UNSIGNED_INT, nullptr);
    }
}

void Renderer::render(const RenderCamera& cam, Vec3 shadowCenter, int width, int height, float time) {
    // Cámara de la luz: orientación fija y centro ajustado a la rejilla de texels, para
    // que las sombras no "tiemblen" al moverse el jugador.
    Mat4 lightView = lookAt(kSunDir * 100.0f, {0, 0, 0}, {0, 1, 0});
    Vec3 c = transformPoint(lightView, shadowCenter);
    float texel = 2.0f * kShadowExtent / float(shadowSize_);
    c.x = std::floor(c.x / texel) * texel;
    c.y = std::floor(c.y / texel) * texel;
    Mat4 lightProj = ortho(c.x - kShadowExtent, c.x + kShadowExtent, c.y - kShadowExtent, c.y + kShadowExtent,
                           -c.z - 80.0f, -c.z + 80.0f);
    Mat4 lightViewProj = lightProj * lightView;

    // Las instancias se suben una vez y sirven para las dos pasadas.
    for (Batch* b : {&boxes_, &spheres_, &highlightBoxes_, &highlightSpheres_}) uploadBatch(*b);
    bool highlight = !highlightBoxes_.items.empty() || !highlightSpheres_.items.empty();
    auto drawHighlighted = [&] {
        drawBatch(highlightBoxes_);
        drawBatch(highlightSpheres_);
    };

    // 1) Sombras: profundidad desde el sol. Se dibujan las caras traseras y con offset
    //    de polígono para evitar el "acné" (auto-sombreado por falta de precisión).
    glBindFramebuffer(GL_FRAMEBUFFER, shadowFbo_);
    glViewport(0, 0, shadowSize_, shadowSize_);
    glClear(GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_FRONT);
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(1.5f, 3.0f);
    glUseProgram(shadowProgram_);
    setMat4(shadowProgram_, "uLightViewProj", lightViewProj);
    drawBatch(boxes_);
    drawBatch(spheres_);
    drawHighlighted();
    if (!skinDraws_.empty()) {
        glUseProgram(skinShadowProgram_);
        setMat4(skinShadowProgram_, "uLightViewProj", lightViewProj);
        drawSkinned(skinShadowProgram_, false);
    }
    glDisable(GL_POLYGON_OFFSET_FILL);
    glBindFramebuffer(GL_FRAMEBUFFER, targetFbo_);
    glViewport(0, 0, width, height);

    // 2) Cielo, detrás de todo (sin escribir profundidad).
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glUseProgram(skyProgram_);
    setVec3(skyProgram_, "uCamForward", cam.forward);
    setVec3(skyProgram_, "uCamRight", cam.right);
    setVec3(skyProgram_, "uCamUp", cam.up);
    setVec3(skyProgram_, "uSunDir", kSunDir);
    glUniform1f(glGetUniformLocation(skyProgram_, "uTime"), time);
    glUniform1f(glGetUniformLocation(skyProgram_, "uWorldColor"), worldColor_);
    float t = std::tan(cam.fovY * 0.5f);
    glUniform2f(glGetUniformLocation(skyProgram_, "uTanHalf"), t * cam.aspect, t);
    glBindVertexArray(emptyVao_);
    glDrawArrays(GL_TRIANGLES, 0, 3);

    // 3) Escena.
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    // Luces: si hay más de las que admite el shader, se quedan las más intensas.
    std::sort(lights_.begin(), lights_.end(), [](const PointLight& a, const PointLight& b) {
        return a.color.x + a.color.y + a.color.z > b.color.x + b.color.y + b.color.z;
    });
    int lightCount = int(std::min<size_t>(lights_.size(), kMaxLights));
    float lightPos[kMaxLights * 4], lightColor[kMaxLights * 3];
    for (int i = 0; i < lightCount; ++i) {
        const PointLight& l = lights_[size_t(i)];
        float* p = lightPos + i * 4;
        float* col = lightColor + i * 3;
        p[0] = l.pos.x; p[1] = l.pos.y; p[2] = l.pos.z; p[3] = l.radius;
        col[0] = l.color.x; col[1] = l.color.y; col[2] = l.color.z;
    }
    int scorchCount = int(scorches_.size() / 5);
    float scorchPos[kMaxScorches * 4], scorchAmount[kMaxScorches];
    for (int i = 0; i < scorchCount; ++i) {
        for (int k = 0; k < 4; ++k) scorchPos[i * 4 + k] = scorches_[size_t(i) * 5 + size_t(k)];
        scorchAmount[i] = scorches_[size_t(i) * 5 + 4];
    }
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, shadowTex_);
    // Uniforms comunes a los dos programas de la escena (instancias y skinning).
    auto sceneUniforms = [&](GLuint prog) {
        glUseProgram(prog);
        setMat4(prog, "uViewProj", cam.proj * cam.view);
        setMat4(prog, "uLightViewProj", lightViewProj);
        setVec3(prog, "uSunDir", kSunDir);
        setVec3(prog, "uCameraPos", cam.position);
        glUniform1f(glGetUniformLocation(prog, "uTime"), time);
        glUniform1i(glGetUniformLocation(prog, "uShadowMap"), 0);
        glUniform1i(glGetUniformLocation(prog, "uLightCount"), lightCount);
        if (lightCount > 0) {
            glUniform4fv(glGetUniformLocation(prog, "uLightPos"), lightCount, lightPos);
            glUniform3fv(glGetUniformLocation(prog, "uLightColor"), lightCount, lightColor);
        }
        glUniform1i(glGetUniformLocation(prog, "uScorchCount"), scorchCount);
        if (scorchCount > 0) {
            glUniform4fv(glGetUniformLocation(prog, "uScorch"), scorchCount, scorchPos);
            glUniform1fv(glGetUniformLocation(prog, "uScorchAmount"), scorchCount, scorchAmount);
        }
        glUniform1f(glGetUniformLocation(prog, "uInflate"), 0.0f);
        glUniform4f(glGetUniformLocation(prog, "uOutlineColor"), 0, 0, 0, 0);
        glUniform1f(glGetUniformLocation(prog, "uWorldColor"), worldColor_);
        glUniform1f(glGetUniformLocation(prog, "uDarkness"), darkness_);
        int litCount = int(litAreas_.size() / 4), darkCount = int(darkAreas_.size() / 4);
        glUniform1i(glGetUniformLocation(prog, "uLitCount"), litCount);
        if (litCount > 0) glUniform4fv(glGetUniformLocation(prog, "uLit"), litCount, litAreas_.data());
        glUniform1i(glGetUniformLocation(prog, "uFocusCount"), darkCount);
        if (darkCount > 0) glUniform4fv(glGetUniformLocation(prog, "uFocus"), darkCount, darkAreas_.data());
        int zoneCount = int(zones_.size() / 4);
        glUniform1i(glGetUniformLocation(prog, "uZoneCount"), zoneCount);
        if (zoneCount > 0) glUniform4fv(glGetUniformLocation(prog, "uZone"), zoneCount, zones_.data());
        int paintCount = int(paints_.size() / 7);
        glUniform1i(glGetUniformLocation(prog, "uPaintCount"), paintCount);
        if (paintCount > 0) {
            float pos[kMaxPaints * 4], col[kMaxPaints * 3];
            for (int i = 0; i < paintCount; ++i) {
                for (int k = 0; k < 4; ++k) pos[i * 4 + k] = paints_[size_t(i) * 7 + size_t(k)];
                for (int k = 0; k < 3; ++k) col[i * 3 + k] = paints_[size_t(i) * 7 + 4 + size_t(k)];
            }
            glUniform4fv(glGetUniformLocation(prog, "uPaint"), paintCount, pos);
            glUniform3fv(glGetUniformLocation(prog, "uPaintColor"), paintCount, col);
        }
    };
    // Personajes: malla con skinning y, detrás, su contorno (la malla inflada con las
    // caras frontales descartadas: solo asoma el borde).
    if (!skinDraws_.empty()) {
        sceneUniforms(skinProgram_);
        drawSkinned(skinProgram_, false);
        glCullFace(GL_FRONT);
        glUniform1f(glGetUniformLocation(skinProgram_, "uInflate"), 0.011f);
        drawSkinned(skinProgram_, true);
        glCullFace(GL_BACK);
    }
    sceneUniforms(meshProgram_);
    drawBatch(boxes_);
    drawBatch(spheres_);

    // 4) Objetivo resaltado con el stencil buffer:
    //    a) se dibuja normal, escribiendo 1 en el stencil donde queda visible;
    //    b) se vuelve a dibujar inflado, con color plano, solo donde el stencil NO es 1:
    //       lo único que sobrevive es un borde alrededor de la silueta completa.
    if (highlight) {
        glEnable(GL_STENCIL_TEST);
        glStencilMask(0xFF);
        glStencilFunc(GL_ALWAYS, 1, 0xFF);
        glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
        drawHighlighted();

        glStencilFunc(GL_NOTEQUAL, 1, 0xFF);
        glStencilMask(0x00);
        glDepthMask(GL_FALSE);
        float pulse = 0.5f + 0.5f * std::sin(time * 9.0f);  // la silueta "late"
        float r = float(highlightColor_ & 0xFF) / 255.0f, g = float((highlightColor_ >> 8) & 0xFF) / 255.0f,
              b = float((highlightColor_ >> 16) & 0xFF) / 255.0f;
        glUniform1f(glGetUniformLocation(meshProgram_, "uInflate"), 0.03f + 0.015f * pulse);
        glUniform4f(glGetUniformLocation(meshProgram_, "uOutlineColor"), lerpf(r, 1.0f, 0.35f * pulse),
                    lerpf(g, 1.0f, 0.35f * pulse), lerpf(b, 1.0f, 0.35f * pulse), 1.0f);
        drawHighlighted();
        glUniform1f(glGetUniformLocation(meshProgram_, "uInflate"), 0.0f);
        glUniform4f(glGetUniformLocation(meshProgram_, "uOutlineColor"), 0, 0, 0, 0);
        glDepthMask(GL_TRUE);
        glStencilMask(0xFF);
        glDisable(GL_STENCIL_TEST);
    }
    glDisable(GL_CULL_FACE);

    // 5) Efectos con alfa premultiplicado: primero el humo (oscurece) y después el fuego
    //    volumétrico (suma luz). Son caros por píxel y blandos, así que se dibujan a media
    //    resolución (4 veces menos píxeles) en un buffer aparte que luego se compone encima.
    //    Ese buffer no tiene profundidad propia: los shaders leen la de la escena.
    int sx0, sy0, sx1, sy1;  // rectángulo de pantalla que cubren los efectos
    if ((!smoke_.empty() || !volumes_.empty()) && fxScreenRect(cam, width, height, sx0, sy0, sx1, sy1)) {
        ensureFxTargets(width, height);
        // Todo el trabajo se limita a ese rectángulo: una mecha encendida no cuesta como
        // un incendio a pantalla completa. El del buffer pequeño lleva 2 texels de margen
        // para que el filtrado bilineal de la composición no lea restos de otro frame.
        glEnable(GL_SCISSOR_TEST);
        glScissor(sx0, sy0, sx1 - sx0, sy1 - sy0);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, targetFbo_);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, depthFbo_);
        glBlitFramebuffer(0, 0, width, height, 0, 0, width, height, GL_DEPTH_BUFFER_BIT, GL_NEAREST);
        glBindFramebuffer(GL_FRAMEBUFFER, fxFbo_);
        glViewport(0, 0, fxWidth_, fxHeight_);
        int fx0 = std::max(sx0 / 2 - 2, 0), fy0 = std::max(sy0 / 2 - 2, 0);
        glScissor(fx0, fy0, std::min(sx1 / 2 + 3, fxWidth_) - fx0, std::min(sy1 / 2 + 3, fxHeight_) - fy0);
        const float transparent[4] = {0, 0, 0, 0};
        glClearBufferfv(GL_COLOR, 0, transparent);  // sin tocar el color de borrado global
        glDisable(GL_DEPTH_TEST);
        glDepthMask(GL_FALSE);
        glEnable(GL_BLEND);
        glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, depthTex_);
        auto fxUniforms = [&](GLuint prog) {
            glUniform1i(glGetUniformLocation(prog, "uSceneDepth"), 1);
            glUniform2f(glGetUniformLocation(prog, "uFxSize"), float(fxWidth_), float(fxHeight_));
            glUniform2f(glGetUniformLocation(prog, "uNearFar"), cam.zNear, cam.zFar);
            glUniform1f(glGetUniformLocation(prog, "uTime"), time);
            setVec3(prog, "uCamPos", cam.position);
        };
        if (!smoke_.empty()) {
            glBindBuffer(GL_ARRAY_BUFFER, fxVbo_);
            glBufferData(GL_ARRAY_BUFFER, smoke_.size() * sizeof(FxInstance), smoke_.data(), GL_STREAM_DRAW);
            glUseProgram(fxProgram_);
            fxUniforms(fxProgram_);
            setMat4(fxProgram_, "uViewProj", cam.proj * cam.view);
            setVec3(fxProgram_, "uCamRight", cam.right);
            setVec3(fxProgram_, "uCamUp", cam.up);
            glBindVertexArray(fxVao_);
            glDrawArraysInstanced(GL_TRIANGLES, 0, 6, GLsizei(smoke_.size()));
        }
        if (!volumes_.empty()) {
            // Se dibujan las caras traseras de cada caja: así se cubre bien tanto si la
            // cámara está fuera como dentro del volumen (el shader calcula la entrada).
            glUseProgram(volumeProgram_);
            fxUniforms(volumeProgram_);
            setMat4(volumeProgram_, "uViewProj", cam.proj * cam.view);
            setVec3(volumeProgram_, "uCamForward", cam.forward);
            glActiveTexture(GL_TEXTURE2);
            glBindTexture(GL_TEXTURE_3D, noiseTex_);
            glUniform1i(glGetUniformLocation(volumeProgram_, "uNoise"), 2);
            glEnable(GL_CULL_FACE);
            glCullFace(GL_FRONT);
            glBindBuffer(GL_ARRAY_BUFFER, volumeVbo_);
            glBufferData(GL_ARRAY_BUFFER, volumes_.size() * sizeof(VolumeInstance), volumes_.data(), GL_STREAM_DRAW);
            glBindVertexArray(volumeVao_);
            glDrawArraysInstanced(GL_TRIANGLES, 0, 36, GLsizei(volumes_.size()));
            glCullFace(GL_BACK);
            glDisable(GL_CULL_FACE);
        }
        glActiveTexture(GL_TEXTURE0);

        // Composición sobre la imagen de la ventana.
        glBindFramebuffer(GL_FRAMEBUFFER, targetFbo_);
        glViewport(0, 0, width, height);
        glScissor(sx0, sy0, sx1 - sx0, sy1 - sy0);
        glUseProgram(compositeProgram_);
        glBindTexture(GL_TEXTURE_2D, fxTex_);
        glUniform1i(glGetUniformLocation(compositeProgram_, "uFx"), 0);
        glBindVertexArray(emptyVao_);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        glDisable(GL_SCISSOR_TEST);
        glDisable(GL_BLEND);
        glDepthMask(GL_TRUE);
        glEnable(GL_DEPTH_TEST);
    }

    glBindVertexArray(0);
    for (Batch* b : {&boxes_, &spheres_, &highlightBoxes_, &highlightSpheres_}) b->items.clear();
    skinDraws_.clear();
    smoke_.clear();
    volumes_.clear();
    lights_.clear();
    scorches_.clear();
    paints_.clear();
    zones_.clear();
    litAreas_.clear();
    darkAreas_.clear();
}
