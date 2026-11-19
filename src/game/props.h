#pragma once

// Piezas del escenario en .glb (assets/props): árboles, rocas, casas, farolas, letreros...
// Cada fichero es una pieza con el origen en el centro de su base, Y hacia arriba y en
// metros. Convenciones de nombres de los nodos (sirven igual desde Blender):
//   - mallas normales: lo que se ve. El material se lee por el nombre: `Glow…` (o con emisión)
//     brilla, `Metal`, `Stone`, `Grass` (hierba con variación), `Water`; el resto, tela.
//     El color sale del color por vértice multiplicado por el color base del material.
//   - `COL_…`: mallas invisibles que hacen de caja de colisión (se usa su caja envolvente).
//     Sin ninguna, la pieza no choca (hierba, flores).
//   - `LUZ_…`: un punto (vacío) que es una fuente de luz del juego: alumbra, se puede viajar a
//     ella y romperla. `LUZ_RRGGBB` le da color (si no, luz cálida).

#include "actors/physics.h"
#include "render/renderer.h"

#include <string>
#include <vector>

struct PropLight {
    Vec3 pos;
    uint32_t color;
};

struct PropDef {
    std::string name;  // el del fichero, sin «.glb»
    std::vector<SkinVertex> verts;
    std::vector<uint32_t> indices;
    AABB bounds;                    // de lo que se ve
    std::vector<AABB> colliders;    // en el espacio de la pieza
    std::vector<PropLight> lights;
    int mesh = -1;                  // malla en el renderer (al subirla)
    std::string path;               // de dónde se lee
    bool loaded = false;            // leída (si no, solo está apuntada)
    bool used = false;              // la usa la zona que está cargada
};

// Las piezas se cargan por zonas: indexDir apunta los .glb de la carpeta sin leerlos y cada zona
// lee al construirse las que use (acquire). Al cambiar de zona, beginZone / endZone sueltan las
// que la nueva ya no usa (y uploadPropMeshes, en el renderer, sube las nuevas y borra esas).
class PropLibrary {
public:
    // Lee todos los .glb de la carpeta (el visor, las pruebas). Devuelve cuántos.
    int loadDir(const std::string& dir);
    // Solo los apunta: se leen al pedirlos. Devuelve cuántos.
    int indexDir(const std::string& dir);
    bool loadFile(const std::string& path, const std::string& name, std::string& error);
    int find(const std::string& name) const;  // -1 si no está (ni apuntada)
    // La pieza, leída y marcada como usada por la zona; -1 si no está o no se puede leer.
    int acquire(const std::string& name);
    void beginZone();  // la zona que se va a construir aún no usa ninguna
    void endZone();    // suelta (de memoria) las leídas que no use
    int loadedCount() const;
    size_t size() const { return defs_.size(); }
    const PropDef& operator[](size_t i) const { return defs_[i]; }
    PropDef& operator[](size_t i) { return defs_[i]; }

private:
    std::vector<PropDef> defs_;
};

PropLibrary& propLibrary();
