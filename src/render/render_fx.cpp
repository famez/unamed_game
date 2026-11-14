// Efectos a media resolución: humo en billboards y fuego volumétrico (raymarching sobre
// una textura 3D de ruido), con sus buffers y el recorte al rectángulo que ocupan.

#include "render/renderer.h"
#include "render/gl_util.h"

#include <algorithm>
#include <cstdio>
#include <vector>

namespace {

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

}  // namespace

// Textura 3D de ruido (R16, 4 MB) para el fuego, generada una vez al arrancar.
void Renderer::createNoiseTexture() {
    std::vector<uint16_t> noise = makeNoiseVolume();
    glGenTextures(1, &noiseTex_);
    glBindTexture(GL_TEXTURE_3D, noiseTex_);
    glTexImage3D(GL_TEXTURE_3D, 0, GL_R16, kNoiseSize, kNoiseSize, kNoiseSize, 0, GL_RED, GL_UNSIGNED_SHORT,
                 noise.data());
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    for (GLenum wrap : {GL_TEXTURE_WRAP_S, GL_TEXTURE_WRAP_T, GL_TEXTURE_WRAP_R})
        glTexParameteri(GL_TEXTURE_3D, wrap, GL_REPEAT);
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

void Renderer::smoke(Vec3 center, float size, float seed, float opacity) {
    smoke_.push_back({{center.x, center.y, center.z}, seed, size, opacity, 0, 0});
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

// Efectos con alfa premultiplicado: primero el humo (oscurece) y después el fuego
// volumétrico (suma luz). Son caros por píxel y blandos, así que se dibujan a media
// resolución (4 veces menos píxeles) en un buffer aparte que luego se compone encima.
// Ese buffer no tiene profundidad propia: los shaders leen la de la escena.
void Renderer::drawEffects(const RenderCamera& cam, int width, int height, float time) {
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
}
