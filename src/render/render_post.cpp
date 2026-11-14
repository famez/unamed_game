// Buffers de la escena (MSAA y resolución interna) y posproceso: bloom en cadena de
// reducciones y composición final sobre la ventana.

#include "render/renderer.h"
#include "render/gl_util.h"

#include <algorithm>
#include <cstdio>

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
