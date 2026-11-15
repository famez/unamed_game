#pragma once

// Escritura de glTF binario (.glb) de un personaje con esqueleto: una malla con skinning
// (posiciones, normales, color por vértice, huesos y pesos) y una armadura con los huesos
// en su pose de reposo. Se abre tal cual en Blender (con la armadura) y el juego lo vuelve a
// leer (hero_model.h: loadCharacterGlb).

#include "core/vmath.h"
#include "render/renderer.h"

#include <string>
#include <vector>

struct GlbOutJoint {
    std::string name;
    int parent;  // -1 = raíz
    Mat4 local;  // respecto al padre, en la pose de reposo
};

struct GlbOutMaterial {
    std::string name;     // Cloth, Metal, Hair, Stone o Glow_RRGGBB (el juego lo usa al leerlo)
    float emissive[3] = {0, 0, 0};
};

struct GlbOutPrimitive {
    int material = 0;
    std::vector<SkinVertex> verts;  // en el espacio del modelo, en la pose de reposo
    std::vector<uint32_t> indices;
};

bool writeSkinnedGlb(const std::string& path, const std::string& name, const std::vector<GlbOutJoint>& joints,
                     const std::vector<GlbOutPrimitive>& prims, const std::vector<GlbOutMaterial>& materials);
