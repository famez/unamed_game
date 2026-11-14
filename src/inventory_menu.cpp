#include "inventory_menu.h"

#include "game.h"

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

const char* kTabNames[] = {"OBJETOS", "MATERIALES", "PRISMAS", "TALLER"};

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
    if (tab_ == Workshop) {  // una fila por receta
        int n = 0;
        const Recipe* r = recipes(n);
        std::vector<ItemId> out;
        for (int i = 0; i < n; ++i) out.push_back(r[i].result);
        return out;
    }
    ItemKind kind = tab_ == Consumables ? ItemKind::Consumable : tab_ == Materials ? ItemKind::Material : ItemKind::Weapon;
    std::vector<ItemId> out;
    for (int i = 0; i < int(ItemId::Count); ++i) {
        ItemId id = ItemId(i);
        // Los consumibles se listan aunque no queden (para poder asignar el atajo).
        bool show = inv.count(id) > 0 || (kind == ItemKind::Consumable);
        ItemKind k = itemDef(id).kind;
        if (k == ItemKind::Upgrade && inv.count(id) > 0) k = ItemKind::Consumable;  // los afiladores, con los objetos
        if (k == kind && show) out.push_back(id);
    }
    return out;
}

void InventoryMenu::activate(Game& game, ItemId id) {
    const ItemDef& d = itemDef(id);
    if (tab_ == Workshop) {
        int n = 0;
        const Recipe* r = recipes(n);
        if (selected_ < n) message_ = game.craft(r[selected_]) ? std::string("Fabricado: ") + d.name : "Faltan piezas";
        messageTime_ = 2.0f;
        return;
    }
    if (d.kind == ItemKind::Upgrade) {
        message_ = game.useItem(id) ? "El prisma tiene más filo" : "No admite más filo";
    } else if (d.kind == ItemKind::Weapon) {
        if (game.equip(id)) message_ = std::string("Equipado: ") + d.name;
    } else if (d.kind == ItemKind::Consumable) {
        if (game.inventory().count(id) <= 0)
            message_ = "No te queda";
        else if (game.useItem(id))
            message_ = std::string("Usado: ") + d.name;
        else
            message_ = "Ahora no hace falta";
    } else {
        message_ = "Los materiales se guardan";
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
                message_ = std::string("Atajo ") + std::to_string(s + 4) + ": " + itemName(id);
                messageTime_ = 2.0f;
            }
    }
    return in.wasPressed(Key::I) || in.wasPressed(Key::Escape) || in.wasPressed(Key::Tab);
}

void InventoryMenu::draw(UiLayer& ui, const Game& game, int width, int height) const {
    DebugDraw& d = ui.shapes;
    const Inventory& inv = game.inventory();
    const uint32_t navy = rgba(14, 18, 46, 240), light = rgba(240, 245, 255), dim = rgba(150, 165, 210);
    const uint32_t cyan = rgba(40, 220, 255), amber = rgba(255, 196, 70);
    TextStyle title;
    title.face = FontFace::Title;
    TextStyle right;
    right.align = Align::Right;

    fillRect(d, 0, 0, float(width), float(height), rgba(8, 10, 30, 150));
    Rect p = panelRect(width, height);
    fillRect(d, p.x0, p.y0, p.x1, p.y1, navy);
    fillRect(d, p.x0, p.y0, p.x1, p.y0 + 44, rgba(24, 34, 84, 250));
    fillRect(d, p.x0, p.y0 + 44, p.x1, p.y0 + 46, cyan);
    strokeRect(d, p.x0, p.y0, p.x1, p.y1, rgba(70, 90, 160));
    title.glow = 0.4f;
    drawText(ui, p.x0 + 24, p.y0 + 8, 26, "INVENTARIO", light, title);
    title.glow = 0;
    char buf[128];
    std::snprintf(buf, sizeof(buf), "PV %d/%d    PM %d/%d    %d LUM", int(std::ceil(game.hp())), int(game.maxHp()),
                  int(game.mp()), int(game.maxMp()), inv.munny);
    TextStyle info = title;
    info.align = Align::Right;
    drawText(ui, p.x1 - 24, p.y0 + 13, 16, buf, amber, info);

    for (int t = 0; t < TabCount; ++t) {
        Rect r = tabRect(t, width, height);
        bool sel = t == tab_;
        fillRect(d, r.x0, r.y0, r.x1, r.y1, sel ? rgba(235, 245, 255, 240) : rgba(30, 40, 90, 230));
        if (sel) fillRect(d, r.x0, r.y1 - 3, r.x1, r.y1, cyan);
        TextStyle tc = title;
        tc.align = Align::Center;
        drawText(ui, (r.x0 + r.x1) * 0.5f, r.y0 + 8, 16, kTabNames[t], sel ? rgba(20, 30, 70) : dim, tc);
    }

    std::vector<ItemId> list = entries(inv);
    int n = std::min(int(list.size()), kMaxRows);
    if (n == 0) drawText(ui, p.x0 + 40, p.y0 + kListTop + 8, 20, "Nada todavía", dim);
    for (int i = 0; i < n; ++i) {
        ItemId id = list[size_t(i)];
        const ItemDef& def = itemDef(id);
        Rect r = rowRect(i, width, height);
        bool sel = i == selected_;
        fillRect(d, r.x0, r.y0, r.x1, r.y1, sel ? rgba(235, 245, 255, 235) : rgba(26, 34, 80, 220));
        if (sel) fillRect(d, r.x0, r.y0, r.x0 + 5, r.y1, cyan);
        gem(d, r.x0 + 24, (r.y0 + r.y1) * 0.5f, 9, def.color);
        uint32_t tc = inv.count(id) > 0 ? (sel ? rgba(20, 30, 70) : light) : rgba(110, 115, 140);
        if (tab_ == Workshop) {  // recetas: en claro las que se pueden fabricar ya
            int rc = 0;
            bool ok = recipes(rc)[i].count > 0 && inv.canCraft(recipes(rc)[i]);
            tc = ok ? (sel ? rgba(20, 30, 70) : light) : rgba(110, 115, 140);
            drawText(ui, r.x0 + 44, r.y0 + 3, 20, def.name, tc);
            drawText(ui, r.x1 - 14, r.y0 + 6, 16, ok ? "listo" : "faltan piezas", ok ? rgba(60, 200, 130) : rgba(150, 120, 140), right);
            continue;
        }
        drawText(ui, r.x0 + 44, r.y0 + 3, 20, def.name, tc);
        if (def.kind == ItemKind::Weapon) {
            if (inv.weapon == id) drawText(ui, r.x1 - 14, r.y0 + 6, 16, "equipado", rgba(60, 200, 130), right);
        } else {
            std::snprintf(buf, sizeof(buf), "×%d", inv.count(id));
            drawText(ui, r.x1 - 14, r.y0 + 4, 19, buf, tc, right);
        }
    }

    // Detalle del objeto seleccionado.
    float dx = p.x0 + 470, dy = p.y0 + kListTop;
    fillRect(d, dx, dy, p.x1 - 30, p.y1 - 120, rgba(26, 34, 80, 220));
    if (n > 0) {
        ItemId id = list[size_t(std::min(selected_, n - 1))];
        const ItemDef& def = itemDef(id);
        gem(d, dx + 40, dy + 40, 22, def.color);
        drawText(ui, dx + 80, dy + 22, 28, def.name, light);
        drawText(ui, dx + 20, dy + 80, 18, def.description, dim);
        float y = dy + 118;
        if (tab_ == Workshop) {  // piezas de la receta: las que tienes / las que hacen falta
            int rc = 0;
            const Recipe& rcp = recipes(rc)[std::min(selected_, n - 1)];
            drawText(ui, dx + 20, y, 17, "PIEZAS", dim, title);
            y += 28;
            for (int k = 0; k < rcp.partCount; ++k) {
                const Recipe::Part& part = rcp.parts[k];
                bool have = inv.count(part.item) >= part.n;
                gem(d, dx + 30, y + 11, 7, itemDef(part.item).color);
                drawText(ui, dx + 46, y, 19, itemName(part.item), have ? light : rgba(255, 130, 140));
                std::snprintf(buf, sizeof(buf), "%d / %d", inv.count(part.item), part.n);
                drawText(ui, dx + 380, y, 19, buf, have ? rgba(110, 255, 160) : rgba(255, 130, 140), right);
                y += 28;
            }
            drawText(ui, dx + 20, y + 8, 16, "Enter · fabricar", amber);
        } else if (def.kind == ItemKind::Weapon) {
            const WeaponStats& w = def.weapon;
            const WeaponStats& cur = inv.weaponStats();
            auto stat = [&](const char* name, float v, float now) {
                drawText(ui, dx + 20, y, 19, name, light);
                std::snprintf(buf, sizeof(buf), "%.1f", double(v));
                drawText(ui, dx + 230, y, 19, buf, light, right);
                if (v != now) drawText(ui, dx + 250, y, 19, v > now ? "+" : "-", v > now ? rgba(110, 255, 140) : rgba(255, 120, 120));
                y += 30;
            };
            stat("Fuerza", w.power, cur.power);
            stat("Magia", w.magic, cur.magic);
            stat("Combo", float(w.comboHits + 1), float(cur.comboHits + 1));
            stat("Alcance", w.reach, cur.reach);
            drawText(ui, dx + 20, y + 8, 16, "Enter · equipar", amber);
        } else if (def.kind == ItemKind::Consumable) {
            drawText(ui, dx + 20, y, 16, "Enter · usar     4 / 5 / 6 · poner en atajo", amber);
        }
    }
    // Atajos actuales.
    float qy = p.y1 - 108;
    drawText(ui, dx, qy, 16, "ATAJOS DEL MENÚ DE COMANDOS", dim, title);
    for (int s = 0; s < Inventory::kQuickSlots; ++s) {
        ItemId id = inv.quick[s];
        std::snprintf(buf, sizeof(buf), "%d  %s ×%d", s + 4, itemName(id), inv.count(id));
        drawText(ui, dx, qy + 24 + float(s) * 22, 17, buf, light);
    }
    if (messageTime_ > 0) drawText(ui, p.x0 + 30, p.y1 - 48, 20, message_.c_str(), rgba(130, 255, 160));
    drawText(ui, p.x0 + 30, p.y1 - 22, 14, "A/D pestaña · W/S elegir · Enter usar/equipar · I cerrar", dim);
}
