#pragma once

#include "debug_draw.h"
#include "platform.h"
#include "text.h"

// Menú de pausa: navegable con teclado (W/S o flechas + Enter) y con el ratón.
class PauseMenu {
public:
    enum class Result { None, Resume, Restart, Quit, QualityChanged };

    bool open = false;
    float sensitivity = 1.0f;  // multiplicador de la sensibilidad del ratón
    int quality = 1;           // calidad gráfica: 0 baja, 1 media, 2 alta

    void show();
    Result update(const Input& in, int width, int height);
    void draw(UiLayer& ui, int width, int height) const;

private:
    enum Item { Continue, Restart, Sensitivity, QualityItem, Quit, ItemCount };

    struct Rect {
        float x0, y0, x1, y1;
        bool contains(float x, float y) const { return x >= x0 && x <= x1 && y >= y0 && y <= y1; }
    };
    static Rect panelRect(int width, int height);
    static Rect itemRect(int item, int width, int height);
    void changeSensitivity(int dir);

    int selected_ = Continue;
};
