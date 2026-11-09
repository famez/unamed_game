#pragma once

// En Linux, libOpenGL.so (glvnd) exporta directamente todas las funciones del core,
// así que no necesitamos un loader (glad/GLEW): basta con pedir los prototipos.
// Si algún día se porta a Windows, aquí es donde entraría glad.
#define GL_GLEXT_PROTOTYPES 1
#include <GL/gl.h>
#include <GL/glext.h>
