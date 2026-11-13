#pragma once

// Objetos del juego y el inventario del jugador. Lógica pura (sin GL): la usan el juego,
// el menú de inventario y los tests.

#include <cstdint>

// Los nombres se muestran con la fuente vectorial del HUD: solo ASCII, sin tildes.
enum class ItemId : int {
    // Consumibles
    Potion, HiPotion, Ether, Elixir,
    // Materiales (los sueltan los enemigos; de momento solo se coleccionan)
    ShinyShard, GoldSpore, WingDust, BlazeGem,
    // Armas
    WoodenSword, StarStaff, AuroraBlade,
    Count
};

enum class ItemKind { Consumable, Material, Weapon };

// Forma del arma al dibujarla.
enum class WeaponStyle { Sword, Staff, Saber };

struct WeaponStats {
    float power = 0;      // daño base de cada golpe
    float magic = 0;      // potencia de las magias
    int comboHits = 0;    // golpes en el suelo antes del remate
    float reach = 0;      // alcance (m) desde el centro del personaje
    float speed = 1;      // multiplicador de la velocidad de los golpes
    float length = 0;     // longitud de la hoja (m)
    uint32_t blade = 0, hilt = 0, trail = 0;
    WeaponStyle style = WeaponStyle::Sword;
};

struct ItemDef {
    const char* name;
    const char* description;
    ItemKind kind;
    uint32_t color;       // color del cristal cuando cae al suelo y del icono
    float heal = 0;       // consumibles: PV que recupera
    float mana = 0;       //              PM que recupera
    WeaponStats weapon;   // armas
};

const ItemDef& itemDef(ItemId id);
inline const char* itemName(ItemId id) { return itemDef(id).name; }

class Inventory {
public:
    static constexpr int kMaxStack = 99;
    static constexpr int kQuickSlots = 3;  // atajos de objetos en el menú de comandos

    Inventory();

    int count(ItemId id) const { return counts_[int(id)]; }
    // Añade hasta el máximo por casilla; devuelve cuántos cupieron.
    int add(ItemId id, int n = 1);
    bool remove(ItemId id, int n = 1);

    int munny = 0;  // dinero
    ItemId weapon = ItemId::WoodenSword;
    ItemId quick[kQuickSlots];
    const WeaponStats& weaponStats() const { return itemDef(weapon).weapon; }

private:
    int counts_[int(ItemId::Count)] = {};
};
