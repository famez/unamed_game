#pragma once

// Lector mínimo de glTF binario (.glb): nodos con su jerarquía y matrices, mallas de
// triángulos (posiciones, normales, índices) y materiales con su color base y su emisión.
// Basta para los modelos que exporta three.js (GLTFExporter) o Blender sin texturas.

#include "core/vmath.h"

#include <cstdint>
#include <string>
#include <vector>

struct GlbMaterial {
    std::string name;
    float base[4] = {1, 1, 1, 1};  // color base en espacio lineal
    float emissive[3] = {0, 0, 0};
    bool doubleSided = false;
};

struct GlbPrimitive {
    std::vector<Vec3> positions, normals;
    std::vector<uint32_t> indices;
    int material = -1;
};

struct GlbNode {
    std::string name;
    int parent = -1;
    int mesh = -1;
    std::vector<int> children;
    Mat4 local = Mat4::identity();
    Mat4 world = Mat4::identity();  // con los padres aplicados
};

struct GlbFile {
    std::vector<GlbNode> nodes;
    std::vector<std::vector<GlbPrimitive>> meshes;
    std::vector<GlbMaterial> materials;
    std::vector<int> roots;  // nodos de la escena

    // Carga el fichero; en caso de error devuelve false y lo explica en `error`.
    bool load(const std::string& path, std::string& error);
    int find(const std::string& name) const;  // índice del nodo con ese nombre (-1 si no hay)
};

// Inversa de una matriz afín (rotación, escala y traslación).
Mat4 affineInverse(const Mat4& a);
