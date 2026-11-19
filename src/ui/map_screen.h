#pragma once

// El mapa del nivel (M, pausa el juego): la zona vista desde arriba (Game::buildLevelMap) y,
// encima, dónde estás y hacia dónde miras, el objetivo, las puertas a otras zonas, el
// teleférico, las cápsulas que quedan, los nodos, los faroles, los portales y quien da
// descanso o vende. Rueda: acercar; arrastrar o WASD: mover; Espacio: centrar en ti.

#include "core/platform.h"
#include "game/level_map.h"
#include "render/image_quad.h"
#include "render/text.h"

class Game;

class MapScreen {
public:
    bool open = false;

    bool init() { return quad_.init(); }
    void shutdown() { quad_.shutdown(); }
    bool ready() const { return !map_.empty(); }
    // Abre el mapa (lo pinta si es de otra zona: hace falta el contexto GL).
    void show(const Game& game, int width, int height);
    // Devuelve true si pide cerrarse.
    bool update(const Input& in, const Game& game, int width, int height, float dt);
    // En tres capas: el fondo (debajo de la imagen), la imagen y lo de encima (marcas y textos).
    // `scale`: píxeles de la ventana por píxel de la interfaz (para recortar la imagen).
    void drawBack(UiLayer& ui, int width, int height) const;
    void drawImage(const Mat4& pixels, float scale, int windowHeight, int width, int height) const;
    void drawFront(UiLayer& ui, const Game& game, float camYaw, int width, int height) const;

    // El rectángulo de la interfaz en el que se ve el mapa.
    struct Rect {
        float x0, y0, x1, y1;
    };
    static Rect viewport(int width, int height);

private:
    void fit(int width, int height);
    float minZoom(int width, int height) const;
    float maxZoom() const;
    void zoomAt(float factor, float sx, float sy, int width, int height);
    void clampView(int width, int height);
    // Del mundo a la pantalla (en la interfaz).
    float sx(Vec3 p, int width, int height) const;
    float sy(Vec3 p, int width, int height) const;

    LevelMap map_;
    ImageQuad quad_;
    float zoom_ = 1;         // píxeles de la interfaz por píxel del mapa
    float cu_ = 0, cv_ = 0;  // el píxel del mapa en el centro de la vista
    bool dragging_ = false;
    float time_ = 0;
};
