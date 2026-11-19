#pragma once

#include "core/platform.h"
#include "game/game.h"
#include "render/text.h"

// Selector de niveles: lo primero que se ve al arrancar (la historia queda aparte por ahora).
// Cada entrada es un nivel suelto para probar mecánicas. W/S o ratón para elegir, Enter o
// clic para entrar. Si
// no caben todas, la lista se desplaza (rueda del ratón, o sola al moverse con W/S).
class LevelSelect {
public:
    struct Entry {
        const char* name;
        const char* about;
        bool test;    // patio de combate aleatorio (modo prueba)
        LevelId level;
    };
    static const Entry* entries(int& count);

    bool open = false;
    void show() { open = true; }
    // Devuelve la entrada elegida (o -1 si aún no hay), y -2 con Esc (cierra el selector).
    int update(const Input& in, int width, int height);
    void draw(UiLayer& ui, int width, int height) const;

private:
    struct Rect {
        float x0, y0, x1, y1;
        bool contains(float x, float y) const { return x >= x0 && x <= x1 && y >= y0 && y <= y1; }
    };
    Rect itemRect(int item, int width, int height) const;
    float listBottom(int height) const;
    float maxScroll(int height) const;
    void keepVisible(int height);
    int selected_ = 0;
    float scroll_ = 0;  // píxeles que la lista sube
};
