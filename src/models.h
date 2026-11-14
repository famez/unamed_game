#pragma once

// Modelos de los personajes. El cuerpo de cada uno se describe con primitivas (elipsoides
// y cápsulas) pegadas a las articulaciones de su esqueleto; al arrancar se funden en una
// sola superficie lisa (unión suave de funciones de distancia), se convierte en malla y
// cada vértice recibe pesos de los huesos cercanos. La GPU la deforma (skinning).
//
// Los detalles que deben verse nítidos o brillar (ojos, líneas de neón, núcleo del pecho,
// mechones con inercia) siguen siendo piezas rígidas aparte (ver game_draw.cpp).

#include "enemy.h"
#include "renderer.h"
#include "skeleton.h"
#include "vmath.h"

#include <cstdint>
#include <vector>

struct BodyPrim {
    int joint;
    bool capsule;    // false: elipsoide con centro `a` y radios `r`; true: cápsula a→b de radio r.x
    Vec3 a, b, r;
    uint32_t color;
    float blend;     // radio de la unión suave con lo anterior (m)
};

struct ModelDef {
    const JointDef* rig;
    int jointCount;
    const BodyPrim* prims;
    int primCount;
    float voxel;  // tamaño de celda (m): más pequeño = más detalle y más triángulos
    // Postura de reposo en la que se genera la malla (nullptr = la del esqueleto). Debe
    // separar las partes que no deben fundirse: brazos abiertos y piernas algo separadas
    // (postura en A); si no, la unión suave pega las manos a la cadera.
    const Pose* bindPose = nullptr;
};

// Malla generada + la inversa de la pose de reposo de cada hueso (para el skinning).
struct BuiltModel {
    int mesh = -1;
    int jointCount = 0;
    Mat4 inverseBind[kMaxJoints];
    size_t triangles = 0;
};

// Postura de reposo de un modelo (rotaciones de su bindPose) en matrices mundo.
void bindMatrices(const ModelDef& def, Mat4* out);
// Genera la malla en la CPU (lógica pura; también la usan los tests).
void buildSkinnedMesh(const ModelDef& def, std::vector<SkinVertex>& verts, std::vector<uint32_t>& indices);

enum class ModelId { Hero, Imp, Shroom, Moth, Count };
// Modelo del cuerpo de un enemigo.
ModelId enemyModel(int enemyKind);
// Formas de un enemigo que no van en su malla (brillan o son demasiado finas) y se dibujan aparte.
bool enemyShapeIsRigid(const EnemyShape& s);
const ModelDef& modelDef(ModelId id);

// Genera y sube todos los modelos (al arrancar).
void buildModels(Renderer& r);
const BuiltModel& model(ModelId id);
// Matrices de skinning (mundo × bind⁻¹) a partir de las matrices mundo de las articulaciones.
void skinMatrices(const BuiltModel& m, const Mat4* joints, Mat4* out);
