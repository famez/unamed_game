#pragma once

#include "debug_draw.h"
#include "items.h"
#include "platform.h"
#include "text.h"

#include <string>
#include <vector>

class Game;

// Menú de inventario (pausa el juego): pestañas de objetos, materiales y armas. Con
// teclado (A/D pestaña, W/S elegir, Enter usar/equipar, 4-6 asignar atajo) o ratón.
class InventoryMenu {
public:
    bool open = false;

    void show();
    // Devuelve true si el menú pide cerrarse.
    bool update(const Input& in, Game& game, int width, int height);
    void draw(UiLayer& ui, const Game& game, int width, int height) const;

private:
    enum Tab { Consumables, Materials, Weapons, Workshop, TabCount };

    struct Rect {
        float x0, y0, x1, y1;
        bool contains(float x, float y) const { return x >= x0 && x <= x1 && y >= y0 && y <= y1; }
    };
    static Rect panelRect(int width, int height);
    static Rect tabRect(int tab, int width, int height);
    static Rect rowRect(int row, int width, int height);
    std::vector<ItemId> entries(const Inventory& inv) const;
    void activate(Game& game, ItemId id);

    int tab_ = Consumables;
    int selected_ = 0;
    std::string message_;
    float messageTime_ = 0;
};
