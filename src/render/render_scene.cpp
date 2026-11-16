// Lo que se pide dibujar cada frame: lotes de cajas y esferas instanciadas, luces, zonas
// (pintura, chamuscado, luz y oscuridad) y las mallas de personajes con skinning.

#include "render/renderer.h"
#include "render/gl_util.h"

#include <algorithm>
#include <cstdio>

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

void Renderer::beginHighlight(uint32_t color) {
    highlighting_ = true;
    highlightColor_ = color;
}

void Renderer::endHighlight() { highlighting_ = false; }

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

void Renderer::updateSkinnedMesh(int mesh, const std::vector<SkinVertex>& verts, const std::vector<uint32_t>& indices) {
    if (mesh < 0 || mesh >= int(skinMeshes_.size())) return;
    SkinMesh& m = skinMeshes_[size_t(mesh)];
    glBindVertexArray(m.vao);
    glBindBuffer(GL_ARRAY_BUFFER, m.vbo);
    glBufferData(GL_ARRAY_BUFFER, GLsizeiptr(verts.size() * sizeof(SkinVertex)), verts.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m.ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, GLsizeiptr(indices.size() * sizeof(uint32_t)), indices.data(), GL_STATIC_DRAW);
    glBindVertexArray(0);
    m.count = GLsizei(indices.size());
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

void Renderer::prop(int mesh, const Mat4& transform, bool shadow) {
    if (mesh < 0 || mesh >= int(skinMeshes_.size())) return;
    if (size_t(mesh) >= props_.size()) props_.resize(size_t(mesh) + 1);
    std::vector<float>& q = shadow ? props_[size_t(mesh)].lit : props_[size_t(mesh)].unshadowed;
    q.insert(q.end(), transform.m, transform.m + 16);
    ++propCount_;
}

// Todas las matrices de las piezas en un solo buffer, agrupadas por malla.
void Renderer::uploadProps() {
    propRanges_.clear();
    size_t total = 0;
    for (const PropQueue& q : props_) total += q.lit.size() + q.unshadowed.size();
    if (total == 0) return;
    std::vector<float> data;
    data.reserve(total);
    for (size_t m = 0; m < props_.size(); ++m) {
        const PropQueue& q = props_[m];
        if (q.lit.empty() && q.unshadowed.empty()) continue;
        PropRange r{int(m), data.size() / 16, q.lit.size() / 16, (q.lit.size() + q.unshadowed.size()) / 16};
        data.insert(data.end(), q.lit.begin(), q.lit.end());
        data.insert(data.end(), q.unshadowed.begin(), q.unshadowed.end());
        propRanges_.push_back(r);
    }
    glBindBuffer(GL_ARRAY_BUFFER, propVbo_);
    size_t bytes = data.size() * sizeof(float);
    if (bytes > propVboCapacity_) {
        propVboCapacity_ = bytes * 2;
        glBufferData(GL_ARRAY_BUFFER, GLsizeiptr(propVboCapacity_), nullptr, GL_STREAM_DRAW);
    }
    glBufferSubData(GL_ARRAY_BUFFER, 0, GLsizeiptr(bytes), data.data());
}

// Una llamada por modelo: la malla con sus matrices como atributo por instancia.
void Renderer::drawProps(bool shadowPass) {
    for (const PropRange& r : propRanges_) {
        size_t count = shadowPass ? r.shadowed : r.total;
        if (count == 0) continue;
        const SkinMesh& m = skinMeshes_[size_t(r.mesh)];
        glBindVertexArray(m.vao);
        glBindBuffer(GL_ARRAY_BUFFER, propVbo_);
        for (int c = 0; c < 4; ++c) {
            GLuint loc = GLuint(6 + c);
            glEnableVertexAttribArray(loc);
            glVertexAttribPointer(loc, 4, GL_FLOAT, GL_FALSE, 64, (void*)(r.first * 64 + size_t(c) * 16));
            glVertexAttribDivisor(loc, 1);
        }
        glDrawElementsInstanced(GL_TRIANGLES, m.count, GL_UNSIGNED_INT, nullptr, GLsizei(count));
    }
    glBindVertexArray(0);
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

void Renderer::portrait(const RenderCamera& cam, int x, int y, int w, int h) {
    if (skinDraws_.empty() || w <= 0 || h <= 0) return;
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(x, y, w, h);
    glEnable(GL_SCISSOR_TEST);
    glScissor(x, y, w, h);
    glDepthMask(GL_TRUE);
    glClear(GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glDisable(GL_BLEND);
    GLuint prog = skinProgram_;
    glUseProgram(prog);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, shadowTex_);
    setMat4(prog, "uViewProj", cam.proj * cam.view);
    setMat4(prog, "uLightViewProj", translate({0, 0, 10}));  // fuera del mapa de sombras: todo iluminado
    setVec3(prog, "uSunDir", normalize(Vec3{0.3f, 0.6f, 0.75f}));
    setVec3(prog, "uCameraPos", cam.position);
    glUniform1f(glGetUniformLocation(prog, "uTime"), 0.0f);
    glUniform1i(glGetUniformLocation(prog, "uShadowMap"), 0);
    for (const char* n : {"uLightCount", "uScorchCount", "uLitCount", "uZoneCount", "uPaintCount"})
        glUniform1i(glGetUniformLocation(prog, n), 0);
    glUniform1f(glGetUniformLocation(prog, "uInflate"), 0.0f);
    glUniform4f(glGetUniformLocation(prog, "uOutlineColor"), 0, 0, 0, 0);
    glUniform1f(glGetUniformLocation(prog, "uWorldColor"), 1.0f);
    glUniform1f(glGetUniformLocation(prog, "uNight"), 0.0f);
    glUniform1f(glGetUniformLocation(prog, "uDarkness"), 0.0f);
    drawSkinned(prog, false);
    glCullFace(GL_FRONT);
    glUniform1f(glGetUniformLocation(prog, "uInflate"), 0.004f);
    drawSkinned(prog, true);
    glCullFace(GL_BACK);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_DEPTH_TEST);
    glViewport(0, 0, windowW_, windowH_);
    skinDraws_.clear();
}
