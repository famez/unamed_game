#pragma once

// Ayudas internas del render, compartidas por sus .cpp (no forman parte de su interfaz).

#include "core/gl.h"
#include "core/vmath.h"

inline void setVec3(GLuint prog, const char* name, Vec3 v) {
    glUniform3f(glGetUniformLocation(prog, name), v.x, v.y, v.z);
}
inline void setMat4(GLuint prog, const char* name, const Mat4& m) {
    glUniformMatrix4fv(glGetUniformLocation(prog, name), 1, GL_FALSE, m.m);
}

// Vértice del cubo y la esfera unidad que comparten todas las instancias.
struct CubeVertex {
    float p[3], n[3];
};
