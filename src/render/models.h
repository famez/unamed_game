#pragma once

// Modelos de los personajes. El cuerpo de cada uno se describe con primitivas (elipsoides
// y cápsulas) pegadas a las articulaciones de su esqueleto; al arrancar se funden en una
// sola superficie lisa (unión suave de funciones de distancia), se convierte en malla y
// cada vértice recibe pesos de los huesos cercanos. La GPU la deforma (skinning).
//
// Los detalles que deben verse nítidos o brillar (ojos, líneas de neón, núcleo del pecho,
// mechones con inercia) siguen siendo piezas rígidas aparte (ver game_draw.cpp).

#include "actors/enemy.h"
#include "render/renderer.h"
#include "actors/skeleton.h"
#include "core/vmath.h"

#include <cstdint>
#include <vector>

struct BodyPrim {
    int joint;
    bool capsule;    // false: elipsoide con centro `a` y radios `r`; true: cápsula a→b de radio r.x
                     // (si r.y ≠ r.x, cono redondeado: radio r.x en `a` y r.y en `b`)
    Vec3 a, b, r;
    uint32_t color;
    float blend;     // radio de la unión suave con lo anterior (m); negativo: talla (resta suave)
    bool paint = false;  // solo pinta (sombreado pintado en los colores), no cambia la forma
    uint8_t mat = 0;     // material del sombreado de personajes (7 = pelo: brillo en banda)
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
    // > 0: los colores de las primitivas se mezclan en esta distancia (m) en lugar de cortarse
    // en seco (piel, barba y labios de una cara); 0: bordes limpios (prendas).
    float colorBlend = 0;
    // > 0: las normales de la piel se acercan a las de una esfera con centro `roundCenter`
    // (espacio de la articulación `roundJoint`): la sombra de la cara cae limpia, sin manchas
    // de la nariz o el mentón (el truco de los juegos con estilo anime).
    float roundNormals = 0;
    int roundJoint = 0;
    Vec3 roundCenter{};
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

enum class ModelId { Hero, Dazzled, Brute, Watcher, Hound, Ilse, HeroHead, Count };  // enemigos: 1 + EnemyKind
// Modelo del cuerpo de un enemigo.
ModelId enemyModel(int enemyKind);
// Formas de un enemigo que no van en su malla (brillan o son demasiado finas) y se dibujan aparte.
bool enemyShapeIsRigid(const EnemyShape& s);
const ModelDef& modelDef(ModelId id);

// Punto de la superficie de la cabeza del protagonista delante de (x, y), en espacio de la
// cabeza (para pegar los rasgos dibujados aparte).
Vec3 headSurface(float x, float y);

// Genera y sube todos los modelos (al arrancar).
void buildModels(Renderer& r);
const BuiltModel& model(ModelId id);
// Matrices de skinning (mundo × bind⁻¹) a partir de las matrices mundo de las articulaciones.
void skinMatrices(const BuiltModel& m, const Mat4* joints, Mat4* out);
