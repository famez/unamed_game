#include "render/image_quad.h"

#include "render/gl_util.h"

#include <cstdio>

namespace {

const char* kVs = R"(#version 330 core
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aUv;
uniform mat4 uPixels;
out vec2 vUv;
void main() {
    vUv = aUv;
    gl_Position = uPixels * vec4(aPos, 0.0, 1.0);
}
)";

const char* kFs = R"(#version 330 core
in vec2 vUv;
uniform sampler2D uImage;
uniform float uAlpha;
out vec4 fragColor;
void main() {
    vec4 c = texture(uImage, vUv);
    fragColor = vec4(c.rgb, c.a * uAlpha);
}
)";

GLuint compile(GLenum type, const char* src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetShaderInfoLog(s, sizeof(log), nullptr, log);
        std::fprintf(stderr, "Error en el shader de imagen:\n%s\n", log);
    }
    return s;
}

}  // namespace

bool ImageQuad::init() {
    program_ = glCreateProgram();
    GLuint v = compile(GL_VERTEX_SHADER, kVs), f = compile(GL_FRAGMENT_SHADER, kFs);
    glAttachShader(program_, v);
    glAttachShader(program_, f);
    glLinkProgram(program_);
    glDeleteShader(v);
    glDeleteShader(f);
    GLint ok = 0;
    glGetProgramiv(program_, GL_LINK_STATUS, &ok);
    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(float) * 16, nullptr, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(float) * 4, nullptr);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(float) * 4, reinterpret_cast<void*>(sizeof(float) * 2));
    glBindVertexArray(0);
    glGenTextures(1, &tex_);
    return ok != 0;
}

void ImageQuad::shutdown() {
    if (tex_) glDeleteTextures(1, &tex_);
    if (vbo_) glDeleteBuffers(1, &vbo_);
    if (vao_) glDeleteVertexArrays(1, &vao_);
    if (program_) glDeleteProgram(program_);
    tex_ = vbo_ = vao_ = program_ = 0;
    width_ = height_ = 0;
}

void ImageQuad::upload(int w, int h, const uint32_t* rgba) {
    glBindTexture(GL_TEXTURE_2D, tex_);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);
    width_ = w, height_ = h;
}

void ImageQuad::draw(const Mat4& pixels, float x0, float y0, float x1, float y1, float alpha) const {
    if (!ready()) return;
    const float v[16] = {x0, y0, 0, 0, x1, y0, 1, 0, x0, y1, 0, 1, x1, y1, 1, 1};
    glUseProgram(program_);
    setMat4(program_, "uPixels", pixels);
    glUniform1i(glGetUniformLocation(program_, "uImage"), 0);
    glUniform1f(glGetUniformLocation(program_, "uAlpha"), alpha);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tex_);
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(v), v);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glBindVertexArray(0);
    glBindTexture(GL_TEXTURE_2D, 0);
}
