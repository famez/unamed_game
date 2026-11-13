#include "items.h"

#include <algorithm>

namespace {

constexpr uint32_t rgb(uint8_t r, uint8_t g, uint8_t b) {
    return uint32_t(r) | uint32_t(g) << 8 | uint32_t(b) << 16 | 0xFF000000u;
}

WeaponStats weapon(float power, float magic, int combo, float reach, float speed, float length, uint32_t blade,
                   uint32_t hilt, uint32_t trail, WeaponStyle style) {
    WeaponStats w;
    w.power = power;
    w.magic = magic;
    w.comboHits = combo;
    w.reach = reach;
    w.speed = speed;
    w.length = length;
    w.blade = blade;
    w.hilt = hilt;
    w.trail = trail;
    w.style = style;
    return w;
}

// clang-format off
const ItemDef kItems[int(ItemId::Count)] = {
    {"Célula vital",     "Recupera 40 PV.",                          ItemKind::Consumable, rgb(90, 230, 120), 40, 0, {}},
    {"Célula vital+",    "Recupera 100 PV.",                         ItemKind::Consumable, rgb(60, 200, 255), 100, 0, {}},
    {"Carga prismática", "Recupera 30 PM.",                          ItemKind::Consumable, rgb(120, 140, 255), 0, 30, {}},
    {"Núcleo puro",      "Recupera todos los PV y PM.",              ItemKind::Consumable, rgb(255, 120, 220), 999, 999, {}},
    {"Estática cristalizada", "Restos de un Rastrero. Aún chisporrotea.", ItemKind::Material,  rgb(255, 245, 150), 0, 0, {}},
    {"Esquirla de domo", "Pesa más de lo que parece. Está fría.",    ItemKind::Material,   rgb(255, 200, 60), 0, 0, {}},
    {"Polvo de eco",     "Al tocarlo se oye un susurro lejano.",     ItemKind::Material,   rgb(200, 160, 255), 0, 0, {}},
    {"Prisma ígneo",     "Una gema tibia. Late como un corazón.",    ItemKind::Material,   rgb(255, 110, 60), 0, 0, {}},
    {"Prisma básico",    "Equilibrado. Combo de 4 golpes.",          ItemKind::Weapon,     rgb(210, 170, 110), 0, 0,
        weapon(10, 4, 3, 1.9f, 1.0f, 0.80f, rgb(222, 184, 120), rgb(120, 80, 45), rgb(120, 200, 255), WeaponStyle::Sword)},
    {"Prisma estelar",   "Magia fuerte. Combo rápido de 5.",         ItemKind::Weapon,     rgb(170, 120, 255), 0, 0,
        weapon(7, 12, 4, 1.8f, 1.2f, 0.95f, rgb(150, 110, 230), rgb(255, 225, 90), rgb(200, 150, 255), WeaponStyle::Staff)},
    {"Prisma aurora",    "Golpes muy fuertes y de largo alcance.",   ItemKind::Weapon,     rgb(255, 150, 200), 0, 0,
        weapon(16, 7, 3, 2.3f, 0.95f, 1.00f, rgb(255, 205, 230), rgb(250, 200, 80), rgb(255, 140, 200), WeaponStyle::Saber)},
};
// clang-format on

}  // namespace

const ItemDef& itemDef(ItemId id) { return kItems[int(id)]; }

Inventory::Inventory() : quick{ItemId::Potion, ItemId::HiPotion, ItemId::Ether} {}

int Inventory::add(ItemId id, int n) {
    int& c = counts_[int(id)];
    int added = std::clamp(kMaxStack - c, 0, n);
    c += added;
    return added;
}

bool Inventory::remove(ItemId id, int n) {
    int& c = counts_[int(id)];
    if (c < n) return false;
    c -= n;
    return true;
}
