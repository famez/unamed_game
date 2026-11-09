#include "debug_draw.h"

#include <cstddef>
#include <cstdio>

namespace {

const char* kVertexShader = R"(#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec4 aColor;
uniform mat4 uViewProj;
uniform float uPointSize;
out vec4 vColor;
void main() {
    vColor = aColor;
    gl_Position = uViewProj * vec4(aPos, 1.0);
    gl_PointSize = uPointSize;
}
)";

const char* kFragmentShader = R"(#version 330 core
in vec4 vColor;
out vec4 fragColor;
void main() {
    fragColor = vColor;
}
)";

GLuint compileShader(GLenum type, const char* src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetShaderInfoLog(s, sizeof(log), nullptr, log);
        std::fprintf(stderr, "Error compilando shader:\n%s\n", log);
        glDeleteShader(s);
        return 0;
    }
    return s;
}

}  // namespace

bool DebugDraw::init() {
    GLuint vs = compileShader(GL_VERTEX_SHADER, kVertexShader);
    GLuint fs = compileShader(GL_FRAGMENT_SHADER, kFragmentShader);
    if (!vs || !fs) return false;

    program_ = glCreateProgram();
    glAttachShader(program_, vs);
    glAttachShader(program_, fs);
    glLinkProgram(program_);
    glDeleteShader(vs);
    glDeleteShader(fs);
    GLint ok = 0;
    glGetProgramiv(program_, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetProgramInfoLog(program_, sizeof(log), nullptr, log);
        std::fprintf(stderr, "Error enlazando programa:\n%s\n", log);
        return false;
    }
    uViewProj_ = glGetUniformLocation(program_, "uViewProj");
    uPointSize_ = glGetUniformLocation(program_, "uPointSize");

    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(DebugVertex),
                          (void*)offsetof(DebugVertex, x));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(DebugVertex),
                          (void*)offsetof(DebugVertex, color));
    glBindVertexArray(0);

    lines_.reserve(16384);
    points_.reserve(1024);
    return true;
}

void DebugDraw::shutdown() {
    if (vbo_) glDeleteBuffers(1, &vbo_);
    if (vao_) glDeleteVertexArrays(1, &vao_);
    if (program_) glDeleteProgram(program_);
    vbo_ = vao_ = program_ = 0;
}

void DebugDraw::line(Vec3 a, Vec3 b, uint32_t color) {
    lines_.push_back({a.x, a.y, a.z, color});
    lines_.push_back({b.x, b.y, b.z, color});
}

void DebugDraw::triangle(Vec3 a, Vec3 b, Vec3 c, uint32_t color) {
    tris_.push_back({a.x, a.y, a.z, color});
    tris_.push_back({b.x, b.y, b.z, color});
    tris_.push_back({c.x, c.y, c.z, color});
}

void DebugDraw::quad(Vec3 a, Vec3 b, Vec3 c, Vec3 d, uint32_t color) {
    triangle(a, b, c, color);
    triangle(a, c, d, color);
}

void DebugDraw::point(Vec3 p, uint32_t color) { points_.push_back({p.x, p.y, p.z, color}); }

void DebugDraw::box(const Mat4& t, Vec3 c, Vec3 h, uint32_t color) {
    Vec3 v[8];
    for (int i = 0; i < 8; ++i) {
        Vec3 corner{c.x + ((i & 1) ? h.x : -h.x), c.y + ((i & 2) ? h.y : -h.y),
                    c.z + ((i & 4) ? h.z : -h.z)};
        v[i] = transformPoint(t, corner);
    }
    // Aristas: pares de esquinas que difieren en un solo bit.
    static const int kEdges[12][2] = {{0, 1}, {2, 3}, {4, 5}, {6, 7}, {0, 2}, {1, 3},
                                      {4, 6}, {5, 7}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};
    for (const auto& e : kEdges) line(v[e[0]], v[e[1]], color);
}

void DebugDraw::circleXZ(Vec3 c, float r, int segments, uint32_t color) {
    Vec3 prev{c.x + r, c.y, c.z};
    for (int i = 1; i <= segments; ++i) {
        float a = 2.0f * kPi * float(i) / float(segments);
        Vec3 cur{c.x + r * std::cos(a), c.y, c.z + r * std::sin(a)};
        line(prev, cur, color);
        prev = cur;
    }
}

void DebugDraw::flush(const Mat4& viewProj, float pointSize) {
    size_t total = tris_.size() + lines_.size() + points_.size();
    if (total == 0) return;

    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    // "Orphaning": pedimos un buffer nuevo cada frame para que el driver no tenga que
    // esperar a que la GPU termine de leer el del frame anterior.
    if (total > gpuCapacity_) gpuCapacity_ = total + total / 2;
    glBufferData(GL_ARRAY_BUFFER, gpuCapacity_ * sizeof(DebugVertex), nullptr, GL_STREAM_DRAW);
    size_t offset = 0;
    for (const auto* list : {&tris_, &lines_, &points_}) {
        glBufferSubData(GL_ARRAY_BUFFER, offset * sizeof(DebugVertex), list->size() * sizeof(DebugVertex),
                        list->data());
        offset += list->size();
    }

    glUseProgram(program_);
    glUniformMatrix4fv(uViewProj_, 1, GL_FALSE, viewProj.m);
    glUniform1f(uPointSize_, pointSize);
    glBindVertexArray(vao_);
    GLint first = 0;
    glDrawArrays(GL_TRIANGLES, first, GLsizei(tris_.size()));
    first += GLint(tris_.size());
    glDrawArrays(GL_LINES, first, GLsizei(lines_.size()));
    first += GLint(lines_.size());
    glDrawArrays(GL_POINTS, first, GLsizei(points_.size()));
    glBindVertexArray(0);

    tris_.clear();
    lines_.clear();
    points_.clear();
}
