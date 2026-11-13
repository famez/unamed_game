#include "inventory_menu.h"

#include "game.h"
#include "text.h"

#include <cstdio>

namespace {

constexpr float kPanelW = 900, kPanelH = 520;
constexpr float kRowH = 34, kListTop = 110, kListW = 400;
constexpr int kMaxRows = 10;

void fillRect(DebugDraw& hud, float x0, float y0, float x1, float y1, uint32_t c) {
    hud.quad({x0, y0, 0}, {x1, y0, 0}, {x1, y1, 0}, {x0, y1, 0}, c);
}

void strokeRect(DebugDraw& hud, float x0, float y0, float x1, float y1, uint32_t c) {
    hud.line({x0, y0, 0}, {x1, y0, 0}, c);
    hud.line({x1, y0, 0}, {x1, y1, 0}, c);
    hud.line({x1, y1, 0}, {x0, y1, 0}, c);
    hud.line({x0, y1, 0}, {x0, y0, 0}, c);
}

// Rombo de color (icono del objeto).
void gem(DebugDraw& hud, float cx, float cy, float r, uint32_t c) {
    hud.quad({cx, cy - r, 0}, {cx + r, cy, 0}, {cx, cy + r, 0}, {cx - r, cy, 0}, c);
    hud.triangle({cx, cy - r, 0}, {cx + r * 0.5f, cy - r * 0.5f, 0}, {cx, cy, 0}, rgba(255, 255, 255, 110));
}

const char* kTabNames[] = {"OBJETOS", "MATERIALES", "ARMAS"};

}  // namespace

InventoryMenu::Rect InventoryMenu::panelRect(int width, int height) {
    float x = (float(width) - kPanelW) * 0.5f, y = (float(height) - kPanelH) * 0.5f;
    return {x, y, x + kPanelW, y + kPanelH};
}

InventoryMenu::Rect InventoryMenu::tabRect(int tab, int width, int height) {
    Rect p = panelRect(width, height);
    float x = p.x0 + 30 + float(tab) * 180;
    return {x, p.y0 + 56, x + 170, p.y0 + 90};
}

InventoryMenu::Rect InventoryMenu::rowRect(int row, int width, int height) {
    Rect p = panelRect(width, height);
    float y = p.y0 + kListTop + float(row) * kRowH;
    return {p.x0 + 30, y, p.x0 + 30 + kListW, y + kRowH - 4};
}

void InventoryMenu::show() {
    open = true;
    selected_ = 0;
    message_.clear();
}

std::vector<ItemId> InventoryMenu::entries(const Inventory& inv) const {
    ItemKind kind = tab_ == Consumables ? ItemKind::Consumable : tab_ == Materials ? ItemKind::Material : ItemKind::Weapon;
    std::vector<ItemId> out;
    for (int i = 0; i < int(ItemId::Count); ++i) {
        ItemId id = ItemId(i);
        // Los consumibles se listan aunque no queden (para poder asignar el atajo).
        bool show = inv.count(id) > 0 || (kind == ItemKind::Consumable);
        if (itemDef(id).kind == kind && show) out.push_back(id);
    }
    return out;
}

void InventoryMenu::activate(Game& game, ItemId id) {
    const ItemDef& d = itemDef(id);
    if (d.kind == ItemKind::Weapon) {
        if (game.equip(id)) message_ = std::string("EQUIPADO: ") + d.name;
    } else if (d.kind == ItemKind::Consumable) {
        if (game.inventory().count(id) <= 0)
            message_ = "NO TE QUEDA";
        else if (game.useItem(id))
            message_ = std::string("USADO: ") + d.name;
        else
            message_ = "NO HACE FALTA AHORA";
    } else {
        message_ = "LOS MATERIALES SE GUARDAN";
    }
    messageTime_ = 2.0f;
}

bool InventoryMenu::update(const Input& in, Game& game, int width, int height) {
    messageTime_ = std::max(0.0f, messageTime_ - 1.0f / 60.0f);
    if (in.wasPressed(Key::A) || in.wasPressed(Key::Left)) {
        tab_ = (tab_ + TabCount - 1) % TabCount;
        selected_ = 0;
    }
    if (in.wasPressed(Key::D) || in.wasPressed(Key::Right)) {
        tab_ = (tab_ + 1) % TabCount;
        selected_ = 0;
    }
    for (int t = 0; t < TabCount; ++t)
        if (in.mouseLeftPressed && tabRect(t, width, height).contains(in.mouseX, in.mouseY)) {
            tab_ = t;
            selected_ = 0;
        }

    std::vector<ItemId> list = entries(game.inventory());
    int n = std::min(int(list.size()), kMaxRows);
    if (n > 0) {
        if (in.wasPressed(Key::W) || in.wasPressed(Key::Up) || in.wheel > 0) selected_ = (selected_ + n - 1) % n;
        if (in.wasPressed(Key::S) || in.wasPressed(Key::Down) || in.wheel < 0) selected_ = (selected_ + 1) % n;
        selected_ = std::min(selected_, n - 1);
        bool clicked = false;
        for (int i = 0; i < n; ++i) {
            if (!rowRect(i, width, height).contains(in.mouseX, in.mouseY)) continue;
            if (in.mouseDX != 0 || in.mouseDY != 0 || in.mouseLeftPressed) selected_ = i;
            if (in.mouseLeftPressed) clicked = true;
        }
        ItemId id = list[size_t(selected_)];
        if (clicked || in.wasPressed(Key::Enter) || in.wasPressed(Key::Space)) activate(game, id);
        // Atajos del menú de comandos: 4, 5, 6.
        const Key slotKeys[] = {Key::N4, Key::N5, Key::N6};
        for (int s = 0; s < Inventory::kQuickSlots; ++s)
            if (in.wasPressed(slotKeys[s]) && itemDef(id).kind == ItemKind::Consumable) {
                game.inventory().quick[s] = id;
                message_ = std::string("ATAJO ") + std::to_string(s + 4) + ": " + itemName(id);
                messageTime_ = 2.0f;
            }
    }
    return in.wasPressed(Key::I) || in.wasPressed(Key::Escape) || in.wasPressed(Key::Tab);
}

void InventoryMenu::draw(DebugDraw& hud, const Game& game, int width, int height) const {
    const Inventory& inv = game.inventory();
    const uint32_t navy = rgba(22, 30, 70, 240), light = rgba(235, 240, 255), dim = rgba(140, 150, 185);
    const uint32_t gold = rgba(255, 210, 80);

    fillRect(hud, 0, 0, float(width), float(height), rgba(10, 15, 40, 150));
    Rect p = panelRect(width, height);
    fillRect(hud, p.x0, p.y0, p.x1, p.y1, navy);
    fillRect(hud, p.x0, p.y0, p.x1, p.y0 + 44, rgba(50, 90, 200, 245));
    strokeRect(hud, p.x0, p.y0, p.x1, p.y1, rgba(120, 160, 255));
    drawText(hud, p.x0 + 24, p.y0 + 12, 3.2f, "INVENTARIO", light);
    char buf[96];
    std::snprintf(buf, sizeof(buf), "PV %d/%d   PM %d/%d   MUNNY %d", int(std::ceil(game.hp())), int(game.maxHp()),
                  int(game.mp()), int(game.maxMp()), inv.munny);
    drawText(hud, p.x1 - 24 - textWidth(buf, 2.0f), p.y0 + 15, 2.0f, buf, gold);

    for (int t = 0; t < TabCount; ++t) {
        Rect r = tabRect(t, width, height);
        bool sel = t == tab_;
        fillRect(hud, r.x0, r.y0, r.x1, r.y1, sel ? rgba(250, 250, 255, 240) : rgba(40, 55, 110, 230));
        drawText(hud, (r.x0 + r.x1 - textWidth(kTabNames[t], 2.2f)) * 0.5f, r.y0 + 10, 2.2f, kTabNames[t],
                 sel ? rgba(30, 40, 90) : dim);
    }

    std::vector<ItemId> list = entries(inv);
    int n = std::min(int(list.size()), kMaxRows);
    if (n == 0) drawText(hud, p.x0 + 40, p.y0 + kListTop + 10, 2.0f, "NADA TODAVIA", dim);
    for (int i = 0; i < n; ++i) {
        ItemId id = list[size_t(i)];
        const ItemDef& d = itemDef(id);
        Rect r = rowRect(i, width, height);
        bool sel = i == selected_;
        fillRect(hud, r.x0, r.y0, r.x1, r.y1, sel ? rgba(250, 250, 255, 235) : rgba(35, 45, 95, 220));
        if (sel) fillRect(hud, r.x0, r.y0, r.x0 + 6, r.y1, gold);
        gem(hud, r.x0 + 24, (r.y0 + r.y1) * 0.5f, 9, d.color);
        uint32_t tc = inv.count(id) > 0 ? (sel ? rgba(30, 40, 90) : light) : rgba(110, 115, 140);
        drawText(hud, r.x0 + 44, r.y0 + 9, 2.0f, d.name, tc);
        if (d.kind == ItemKind::Weapon) {
            if (inv.weapon == id) drawText(hud, r.x1 - 14 - textWidth("EQUIPADA", 1.6f), r.y0 + 10, 1.6f, "EQUIPADA", rgba(70, 170, 90));
        } else {
            std::snprintf(buf, sizeof(buf), "X%d", inv.count(id));
            drawText(hud, r.x1 - 14 - textWidth(buf, 2.0f), r.y0 + 9, 2.0f, buf, tc);
        }
    }

    // Detalle del objeto seleccionado.
    float dx = p.x0 + 470, dy = p.y0 + kListTop;
    fillRect(hud, dx, dy, p.x1 - 30, p.y1 - 120, rgba(35, 45, 95, 220));
    if (n > 0) {
        ItemId id = list[size_t(std::min(selected_, n - 1))];
        const ItemDef& d = itemDef(id);
        gem(hud, dx + 40, dy + 40, 22, d.color);
        drawText(hud, dx + 80, dy + 26, 3.0f, d.name, light);
        drawText(hud, dx + 20, dy + 84, 1.8f, d.description, dim);
        float y = dy + 120;
        if (d.kind == ItemKind::Weapon) {
            const WeaponStats& w = d.weapon;
            const WeaponStats& cur = inv.weaponStats();
            auto stat = [&](const char* name, float v, float now) {
                std::snprintf(buf, sizeof(buf), "%-8s %4.1f", name, double(v));
                drawText(hud, dx + 20, y, 2.2f, buf, light);
                if (v != now) {
                    const char* arrow = v > now ? "+" : "-";
                    drawText(hud, dx + 260, y, 2.2f, arrow, v > now ? rgba(110, 255, 140) : rgba(255, 120, 120));
                }
                y += 30;
            };
            stat("FUERZA", w.power, cur.power);
            stat("MAGIA", w.magic, cur.magic);
            stat("COMBO", float(w.comboHits + 1), float(cur.comboHits + 1));
            stat("ALCANCE", w.reach, cur.reach);
            drawText(hud, dx + 20, y + 10, 1.6f, "ENTER: EQUIPAR", gold);
        } else if (d.kind == ItemKind::Consumable) {
            drawText(hud, dx + 20, y, 1.6f, "ENTER: USAR    4 / 5 / 6: PONER EN ATAJO", gold);
        }
    }
    // Atajos actuales.
    float qy = p.y1 - 105;
    drawText(hud, dx, qy, 1.8f, "ATAJOS DEL MENU DE COMANDOS", dim);
    for (int s = 0; s < Inventory::kQuickSlots; ++s) {
        ItemId id = inv.quick[s];
        std::snprintf(buf, sizeof(buf), "%d %s X%d", s + 4, itemName(id), inv.count(id));
        drawText(hud, dx, qy + 24 + float(s) * 22, 1.8f, buf, light);
    }
    if (messageTime_ > 0)
        drawText(hud, p.x0 + 30, p.y1 - 40, 2.2f, message_.c_str(), rgba(130, 255, 160));
    const char* hint = "A/D: PESTANA   W/S: ELEGIR   ENTER: USAR/EQUIPAR   I: CERRAR";
    drawText(hud, p.x0 + 30, p.y1 - 18, 1.4f, hint, dim);
}
