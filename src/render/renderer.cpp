#include "render/renderer.h"
#include "render/gl_util.h"

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <string>

namespace {

constexpr float kShadowExtent = 30.0f;  // semilado (m) del área con sombras alrededor del jugador
const Vec3 kSunDir = normalize(Vec3{0.45f, 0.8f, 0.35f});  // hacia el sol

// Fuentes GLSL de render/shaders/, metidas en el binario al compilar (kMeshVs, kMeshFs…).
#include "shaders.inc"

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

 

}  // namespace

bool Renderer::init(Quality q) {
    meshProgram_ = link(kMeshVs, kMeshFs, true);
    skinProgram_ = link(kSkinVs, kMeshFs, true);
    skinShadowProgram_ = link(kSkinShadowVs, kShadowFs, false);
    if (!skinProgram_ || !skinShadowProgram_) return false;
    propProgram_ = link(kPropVs, kMeshFs, true);
    propShadowProgram_ = link(kPropShadowVs, kShadowFs, false);
    if (!propProgram_ || !propShadowProgram_) return false;
    glGenBuffers(1, &propVbo_);
    shadowProgram_ = link(kShadowVs, kShadowFs, false);
    skyProgram_ = link(kSkyVs, kSkyFs, true);
    waterProgram_ = link(kWaterVs, kWaterFs, true);
    createWaterGrid();
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

    createNoiseTexture();

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
    glDeleteProgram(propProgram_);
    glDeleteProgram(propShadowProgram_);
    glDeleteBuffers(1, &propVbo_);
    for (SkinMesh& m : skinMeshes_) {
        glDeleteBuffers(1, &m.vbo);
        glDeleteBuffers(1, &m.ebo);
        glDeleteVertexArrays(1, &m.vao);
    }
    glDeleteProgram(shadowProgram_);
    glDeleteProgram(skyProgram_);
    glDeleteProgram(waterProgram_);
    glDeleteVertexArrays(1, &waterVao_);
    glDeleteBuffers(1, &waterVbo_);
    glDeleteBuffers(1, &waterEbo_);
    if (seaFloorTex_) glDeleteTextures(1, &seaFloorTex_);
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
    uploadProps();
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
    if (!propRanges_.empty()) {
        glUseProgram(propShadowProgram_);
        setMat4(propShadowProgram_, "uLightViewProj", lightViewProj);
        drawProps(true);
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
    glUniform1f(glGetUniformLocation(skyProgram_, "uNight"), night_);
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
        glUniform1f(glGetUniformLocation(prog, "uNight"), night_);
        glUniform1f(glGetUniformLocation(prog, "uFogScale"), fogScale_);
        glUniform1f(glGetUniformLocation(prog, "uDarkness"), darkness_);
        int litCount = int(litAreas_.size() / 4);
        glUniform1i(glGetUniformLocation(prog, "uLitCount"), litCount);
        if (litCount > 0) glUniform4fv(glGetUniformLocation(prog, "uLit"), litCount, litAreas_.data());
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
    if (!propRanges_.empty()) {
        sceneUniforms(propProgram_);
        glUniform1f(glGetUniformLocation(propProgram_, "uPropScenery"), propScenery_ ? 1.0f : 0.0f);
        glUniform1f(glGetUniformLocation(propProgram_, "uSeaLevel"), seaOn_ ? seaLevel_ : -1000.0f);
        drawProps(false);
    }
    sceneUniforms(meshProgram_);
    glUniform1f(glGetUniformLocation(meshProgram_, "uSeaLevel"), seaOn_ ? seaLevel_ : -1000.0f);
    drawBatch(boxes_);
    drawBatch(spheres_);
    if (seaOn_ && waterProgram_) {  // el agua, encima de todo lo opaco (deja ver el fondo)
        sceneUniforms(waterProgram_);
        drawSea();
        glUseProgram(meshProgram_);
    }

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

    // 5) Humo y fuego (render_fx.cpp).
    drawEffects(cam, width, height, time);

    glBindVertexArray(0);
    for (Batch* b : {&boxes_, &spheres_, &highlightBoxes_, &highlightSpheres_}) b->items.clear();
    skinDraws_.clear();
    for (PropQueue& q : props_) q.lit.clear(), q.unshadowed.clear();
    propCount_ = 0;
    smoke_.clear();
    volumes_.clear();
    lights_.clear();
    seaOn_ = false;
    scorches_.clear();
    paints_.clear();
    zones_.clear();
    litAreas_.clear();
}

// --- El mar ----------------------------------------------------------------------------------

void Renderer::createWaterGrid() {
    const int n = 220;  // vértices por lado (la densidad la reparte el shader: más cerca)
    std::vector<float> grid;
    grid.reserve(size_t(n) * size_t(n) * 2);
    for (int z = 0; z < n; ++z)
        for (int x = 0; x < n; ++x) grid.push_back(float(x) / float(n - 1) * 2 - 1), grid.push_back(float(z) / float(n - 1) * 2 - 1);
    std::vector<uint32_t> idx;
    idx.reserve(size_t(n - 1) * size_t(n - 1) * 6);
    for (int z = 0; z + 1 < n; ++z)
        for (int x = 0; x + 1 < n; ++x) {
            uint32_t a = uint32_t(z * n + x), b = a + 1, c = a + uint32_t(n), d = c + 1;
            idx.insert(idx.end(), {a, c, b, b, c, d});
        }
    glGenVertexArrays(1, &waterVao_);
    glGenBuffers(1, &waterVbo_);
    glGenBuffers(1, &waterEbo_);
    glBindVertexArray(waterVao_);
    glBindBuffer(GL_ARRAY_BUFFER, waterVbo_);
    glBufferData(GL_ARRAY_BUFFER, GLsizeiptr(grid.size() * sizeof(float)), grid.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, waterEbo_);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, GLsizeiptr(idx.size() * sizeof(uint32_t)), idx.data(), GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 8, nullptr);
    glBindVertexArray(0);
    waterCount_ = GLsizei(idx.size());
}

void Renderer::setSeaFloor(const std::vector<float>& heights, int nx, int nz, float x0, float z0, float cell, uint32_t version) {
    if (version == seaFloorVersion_ || heights.empty()) return;
    seaFloorVersion_ = version;
    if (!seaFloorTex_) glGenTextures(1, &seaFloorTex_);
    glBindTexture(GL_TEXTURE_2D, seaFloorTex_);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R32F, nx, nz, 0, GL_RED, GL_FLOAT, heights.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);
    // El centro de los texels de las esquinas cae en los puntos de las esquinas del relieve.
    float w = float(nx - 1) * cell, h = float(nz - 1) * cell;
    seaRect_[0] = x0 - 0.5f * cell;
    seaRect_[1] = z0 - 0.5f * cell;
    seaRect_[2] = w + cell;
    seaRect_[3] = h + cell;
}

void Renderer::sea(float level, float waves, float reach) {
    seaOn_ = seaFloorTex_ != 0;
    seaLevel_ = level;
    seaWaves_ = waves;
    seaReach_ = reach;
}

void Renderer::drawSea() {
    GLuint p = waterProgram_;
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, seaFloorTex_);
    glUniform1i(glGetUniformLocation(p, "uFloor"), 1);
    glUniform4fv(glGetUniformLocation(p, "uFloorRect"), 1, seaRect_);
    glUniform1f(glGetUniformLocation(p, "uSeaLevel"), seaLevel_);
    glUniform1f(glGetUniformLocation(p, "uWaves"), seaWaves_);
    glUniform1f(glGetUniformLocation(p, "uReach"), seaReach_);
    glEnable(GL_BLEND);
    // El alfa del destino es la máscara del bloom: se deja como está.
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ZERO, GL_ONE);
    glDisable(GL_CULL_FACE);
    glBindVertexArray(waterVao_);
    glDrawElements(GL_TRIANGLES, waterCount_, GL_UNSIGNED_INT, nullptr);
    glBindVertexArray(0);
    glEnable(GL_CULL_FACE);
    glDisable(GL_BLEND);
    glActiveTexture(GL_TEXTURE0);
}
