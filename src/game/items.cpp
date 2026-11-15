#include "game/items.h"

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
WeaponStats sword(float power, int combo, float reach, float speed, float length, float width, float curve,
                  uint32_t blade, uint32_t hilt, uint32_t trail) {
    WeaponStats w = weapon(power, 5, combo, reach, speed, length, blade, hilt, trail, WeaponStyle::Sword);
    w.width = width;
    w.curve = curve;
    return w;
}

WeaponStats shadowWeapon(WeaponStats w) {
    w.shadow = true;
    return w;
}

// clang-format off
const ItemDef kItems[int(ItemId::Count)] = {
    {"Célula vital",     "Recupera 40 PV.",                          ItemKind::Consumable, rgb(90, 230, 120), 40, 0, {}},
    {"Célula vital+",    "Recupera 100 PV.",                         ItemKind::Consumable, rgb(60, 200, 255), 100, 0, {}},
    {"Poción de poder",  "Recupera 40 de Poder.",                        ItemKind::Consumable, rgb(200, 140, 255), 0, 40, {}},
    {"Núcleo puro",      "Recupera toda la vida y el Poder.",            ItemKind::Consumable, rgb(255, 120, 220), 999, 999, {}},
    {"Estática cristalizada", "Restos de un Rastrero. Aún chisporrotea.", ItemKind::Material,  rgb(255, 245, 150), 0, 0, {}},
    {"Esquirla de domo", "Pesa más de lo que parece. Está fría.",    ItemKind::Material,   rgb(255, 200, 60), 0, 0, {}},
    {"Polvo de eco",     "Al tocarlo se oye un susurro lejano.",     ItemKind::Material,   rgb(200, 160, 255), 0, 0, {}},
    {"Prisma ígneo",     "Una gema tibia. Late como un corazón.",    ItemKind::Material,   rgb(255, 110, 60), 0, 0, {}},
    {"Espada de luz",    "Luz. Una hoja de luz sólida. Combo de 4 golpes.", ItemKind::Weapon, rgb(255, 226, 160), 0, 0,
        weapon(10, 4, 3, 1.9f, 1.0f, 0.82f, rgb(236, 232, 220), rgb(170, 130, 60), rgb(255, 214, 140), WeaponStyle::Sword)},
    {"Prisma estelar",   "Magia fuerte. Combo rápido de 5.",         ItemKind::Weapon,     rgb(170, 120, 255), 0, 0,
        weapon(7, 12, 4, 1.8f, 1.2f, 0.95f, rgb(236, 230, 252), rgb(255, 225, 90), rgb(170, 120, 255), WeaponStyle::Staff)},
    {"Prisma aurora",    "Golpes muy fuertes y de largo alcance.",   ItemKind::Weapon,     rgb(255, 150, 200), 0, 0,
        weapon(16, 7, 3, 2.3f, 0.95f, 1.00f, rgb(255, 236, 246), rgb(250, 200, 80), rgb(255, 80, 190), WeaponStyle::Saber)},
    {"Lanza de luz",     "Estocadas de gran alcance. Combo de 4.",   ItemKind::Weapon,     rgb(120, 230, 255), 0, 0,
        weapon(12, 5, 3, 2.7f, 0.9f, 1.40f, rgb(230, 245, 255), rgb(200, 205, 220), rgb(60, 230, 255), WeaponStyle::Lance)},
    {"Guanteletes de luz", "Puñetazos rapidísimos. Combo de 6.",     ItemKind::Weapon,     rgb(255, 170, 90), 0, 0,
        weapon(6, 6, 5, 1.5f, 1.5f, 0.20f, rgb(255, 240, 225), rgb(200, 205, 220), rgb(255, 160, 60), WeaponStyle::Fists)},
    {"Filo de farola",   "Forjada con el vidrio de una farola. Más filo y el mismo peso.", ItemKind::Weapon, rgb(240, 196, 120), 0, 0,
        sword(14, 3, 2.0f, 1.05f, 0.88f, 0.042f, 0.0f, rgb(196, 190, 176), rgb(92, 64, 40), rgb(255, 190, 110))},
    {"Hoja crepuscular", "Un sable curvo, rápido. Corta como cae la tarde.", ItemKind::Weapon, rgb(230, 130, 110), 0, 0,
        sword(17, 4, 2.1f, 1.15f, 0.95f, 0.036f, 0.14f, rgb(206, 196, 200), rgb(70, 40, 52), rgb(255, 120, 90))},
    {"Espada del vigía", "Larga y pesada: barre a los que rodean.", ItemKind::Weapon, rgb(170, 190, 220), 0, 0,
        sword(22, 3, 2.5f, 0.88f, 1.18f, 0.05f, 0.0f, rgb(178, 186, 200), rgb(40, 44, 58), rgb(170, 210, 255))},
    {"Colmillo de sombra", "Sombra. Corta y negra: cinco tajos seguidos sin respirar.", ItemKind::Weapon, rgb(150, 90, 230), 0, 0,
        shadowWeapon(sword(18, 5, 1.8f, 1.4f, 0.7f, 0.034f, 0.1f, rgb(46, 40, 60), rgb(30, 26, 36), rgb(160, 90, 255)))},
    {"La hora quieta",   "Mitad luz, mitad sombra. Pesa lo que pesa un instante.", ItemKind::Weapon, rgb(255, 240, 200), 0, 0,
        sword(28, 4, 2.4f, 1.1f, 1.08f, 0.046f, 0.06f, rgb(236, 232, 222), rgb(24, 22, 30), rgb(255, 246, 220))},
    {"Mazo de sombra",   "Sombra. Lento y brutal: mazazo, barrido y un salto que agrieta el suelo.", ItemKind::Weapon, rgb(130, 70, 220), 0, 0,
        shadowWeapon(weapon(15, 4, 2, 2.2f, 0.85f, 0.62f, rgb(40, 34, 52), rgb(70, 52, 40), rgb(150, 80, 255), WeaponStyle::Hammer))},
    {"Garras del vacío", "Sombra. Tres cuchillas en cada mano. Combo de 6, muy rápido.", ItemKind::Weapon, rgb(170, 100, 255), 0, 0,
        shadowWeapon(weapon(8, 5, 5, 1.6f, 1.6f, 0.2f, rgb(30, 26, 40), rgb(24, 20, 30), rgb(180, 110, 255), WeaponStyle::Claws))},
    {"Guadaña del eclipse", "Sombra. Largo alcance; los barridos alcanzan a los que rodean.", ItemKind::Weapon, rgb(120, 60, 200), 0, 0,
        shadowWeapon(weapon(16, 7, 3, 2.7f, 0.95f, 1.3f, rgb(54, 46, 66), rgb(40, 30, 30), rgb(140, 70, 255), WeaponStyle::Scythe))},
    {"Martillo del abismo", "Sombra. Una cabeza de piedra negra que pesa como una noche entera.", ItemKind::Weapon, rgb(90, 40, 170), 0, 0,
        shadowWeapon(weapon(26, 6, 2, 2.4f, 0.75f, 0.72f, rgb(24, 20, 30), rgb(60, 40, 30), rgb(120, 50, 255), WeaponStyle::Hammer))},
    {"Afilador de luz",  "Fuerza +2 permanente para cualquier prisma (máximo +6).", ItemKind::Upgrade, rgb(255, 255, 255), 0, 0, {}},
    {"Farolillo de Nora", "T: llama al eco de Nora. Su luz cura y aturde a los Grises.", ItemKind::Key, rgb(255, 190, 110), 0, 0, {}},
};

using P = Recipe::Part;
const Recipe kRecipes[] = {
    {ItemId::HiPotion, 1, 1, {P{ItemId::Potion, 2}, P{ItemId::GoldSpore, 1}}, 2},
    {ItemId::Elixir, 1, 1, {P{ItemId::BlazeGem, 1}, P{ItemId::WingDust, 2}, P{ItemId::ShinyShard, 2}}, 3},
    {ItemId::Ether, 2, 1, {P{ItemId::WingDust, 2}}, 1},
    {ItemId::Sharpener, 1, 1, {P{ItemId::ShinyShard, 2}, P{ItemId::WingDust, 1}}, 2},
    {ItemId::LightLance, 1, 1, {P{ItemId::ShinyShard, 3}, P{ItemId::GoldSpore, 2}}, 2},
    {ItemId::LightFists, 1, 1, {P{ItemId::WingDust, 3}, P{ItemId::ShinyShard, 2}}, 2},
    {ItemId::AuroraBlade, 1, 1, {P{ItemId::StarStaff, 1}, P{ItemId::BlazeGem, 1}, P{ItemId::GoldSpore, 3}}, 3},
    // Espadas: cada una pide un nivel y piezas de los enemigos de la zona.
    {ItemId::LampEdge, 1, 3, {P{ItemId::ShinyShard, 3}, P{ItemId::GoldSpore, 1}}, 2},
    {ItemId::DuskBlade, 1, 6, {P{ItemId::ShinyShard, 4}, P{ItemId::WingDust, 3}}, 2},
    {ItemId::WatchSword, 1, 9, {P{ItemId::GoldSpore, 5}, P{ItemId::WingDust, 2}, P{ItemId::ShinyShard, 2}}, 3},
    {ItemId::ShadowFang, 1, 12, {P{ItemId::WingDust, 6}, P{ItemId::GoldSpore, 2}}, 2},
    // Armas de la sombra.
    {ItemId::ShadowClaws, 1, 3, {P{ItemId::WingDust, 3}, P{ItemId::ShinyShard, 1}}, 2},
    {ItemId::EclipseScythe, 1, 6, {P{ItemId::WingDust, 4}, P{ItemId::GoldSpore, 3}}, 2},
    {ItemId::AbyssHammer, 1, 10, {P{ItemId::GoldSpore, 6}, P{ItemId::WingDust, 4}}, 2},
    {ItemId::StillHour, 1, 16, {P{ItemId::BlazeGem, 1}, P{ItemId::ShinyShard, 6}, P{ItemId::WingDust, 6}, P{ItemId::GoldSpore, 4}}, 4},
};
// clang-format on

}  // namespace

const ItemDef& itemDef(ItemId id) { return kItems[int(id)]; }

const Recipe* recipes(int& count) {
    count = int(sizeof(kRecipes) / sizeof(kRecipes[0]));
    return kRecipes;
}

bool Inventory::canCraft(const Recipe& r, int level) const {
    if (level < r.minLevel) return false;
    for (int i = 0; i < r.partCount; ++i)
        if (count(r.parts[i].item) < r.parts[i].n) return false;
    // Un arma solo se fabrica una vez.
    if (itemDef(r.result).kind == ItemKind::Weapon && count(r.result) > 0) return false;
    return count(r.result) + r.count <= kMaxStack;
}

bool Inventory::craft(const Recipe& r, int level) {
    if (!canCraft(r, level)) return false;
    for (int i = 0; i < r.partCount; ++i) remove(r.parts[i].item, r.parts[i].n);
    add(r.result, r.count);
    return true;
}

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
