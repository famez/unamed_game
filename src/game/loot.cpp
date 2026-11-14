// Botín y progreso: lo que sueltan los enemigos, recogerlo (lúmenes, vida, experiencia,
// objetos), subir de nivel, curarse, fabricar, usar y equipar.

#include "game/game.h"

#include <algorithm>

namespace {

constexpr float kHpOrb = 8, kMpOrb = 6;

}  // namespace

void Game::dropLoot(Vec3 pos, int munny, int hpOrbs, int mpOrbs, ItemId item, bool withItem, bool shadow) {
    auto pop = [&](PickupKind kind, ItemId id, int value) {
        float a = random01() * 2.0f * kPi, r = randRange(1.2f, 2.8f);
        Pickup p;
        p.kind = kind;
        p.item = id;
        p.value = value;
        p.pos = pos;
        p.vel = {std::cos(a) * r, randRange(4.5f, 6.5f), std::sin(a) * r};
        p.seed = random01() * 10.0f;
        pickups_.push_back(p);
    };
    while (munny > 0) {  // monedas de 10, 5 y 1
        int v = munny >= 10 ? 10 : munny >= 5 ? 5 : 1;
        pop(PickupKind::Munny, ItemId::Potion, v);
        munny -= v;
    }
    for (int i = 0; i < hpOrbs; ++i) pop(PickupKind::Hp, ItemId::Potion, 0);
    for (int i = 0; i < mpOrbs; ++i) pop(shadow ? PickupKind::Shadow : PickupKind::Mp, ItemId::Potion, 0);
    if (withItem) pop(PickupKind::Item, item, 1);
}

void Game::updatePickups(float dt) {
    Vec3 chest = hero_.body.pos + Vec3{0, 0.9f, 0};
    bool heroOk = hero_.downTimer < 0;
    for (Pickup& p : pickups_) {
        p.age += dt;
        if (p.age > (p.kind == PickupKind::Item ? 60.0f : 25.0f)) {
            p.dead = true;
            continue;
        }
        Vec3 to = chest - p.pos;
        float dist = length(to);
        float magnet = p.kind == PickupKind::Item ? 2.5f : p.kind == PickupKind::Xp ? 14.0f : 4.5f;
        if (heroOk && p.age > 0.4f && dist < magnet) {
            // Atraído por el jugador, cada vez más rápido.
            p.resting = false;
            float spd = 6.0f + (magnet - dist) * 5.0f;
            p.vel = p.vel + (to * (spd / std::max(dist, 1e-3f)) - p.vel) * std::min(1.0f, 10.0f * dt);
            p.pos += p.vel * dt;
            if (length(chest - p.pos) < 0.5f) collect(p);
            continue;
        }
        if (p.resting) continue;
        p.vel.y -= kGravity * dt;
        p.vel.x *= std::exp(-0.8f * dt);
        p.vel.z *= std::exp(-0.8f * dt);
        Vec3 next = p.pos + p.vel * dt;
        RayHit h;
        if (segmentVsWorld(p.pos, next, world_, h)) {
            Vec3 n = h.normal;
            p.pos = p.pos + (next - p.pos) * h.t + n * 0.01f;
            p.vel = (p.vel - n * (1.5f * dot(p.vel, n))) * 0.55f;
            if (n.y > 0.7f && length(p.vel) < 1.2f) {
                p.resting = true;
                p.vel = {};
            }
        } else {
            p.pos = next;
        }
    }
    pickups_.erase(std::remove_if(pickups_.begin(), pickups_.end(), [](const Pickup& p) { return p.dead; }),
                   pickups_.end());
}

void Game::collect(Pickup& p) {
    p.dead = true;
    uint32_t col = rgba(255, 255, 255);
    switch (p.kind) {
        case PickupKind::Hp: heal(kHpOrb, 0); col = rgba(120, 255, 140); uiSound(Sfx::Pickup, 0.5f, 1.0f); break;
        case PickupKind::Mp: heal(0, kMpOrb); col = rgba(255, 226, 160); uiSound(Sfx::Pickup, 0.5f, 1.25f); break;
        case PickupKind::Shadow:
            shadow_ = std::min(maxShadow_, shadow_ + kMpOrb);
            col = rgba(150, 80, 255);
            uiSound(Sfx::Pickup, 0.5f, 0.8f);
            break;
        case PickupKind::Xp:
            gainXp(p.value);
            col = rgba(190, 240, 255);
            uiSound(Sfx::Pickup, 0.3f, 1.5f + 0.02f * float(xp_ % 10));
            break;
        case PickupKind::Munny:
            inv_.munny += p.value;
            col = rgba(255, 220, 80);
            uiSound(Sfx::Coin, 0.45f, 0.9f + 0.03f * float(kills_ % 8));
            break;
        case PickupKind::Item:
            uiSound(Sfx::ItemGet, 0.8f);
            col = itemDef(p.item).color;
            if (inv_.add(p.item) > 0)
                notify(std::string("Obtenido: ") + itemName(p.item), col);
            else
                notify(std::string("No cabe más: ") + itemName(p.item), rgba(180, 180, 190));
            break;
    }
    spawnSparks(p.pos, {0, 1, 0}, 4, col, 1.5f);
}

void Game::gainXp(int amount) {
    if (heroLevel_ >= kMaxLevel) return;
    xp_ += amount;
    while (heroLevel_ < kMaxLevel && xp_ >= xpToNext(heroLevel_)) {
        xp_ -= xpToNext(heroLevel_);
        ++heroLevel_;
        // Cada nivel: más vida (y llena), más fuerza y algo más de Luz y Sombra.
        maxHp_ += 10;
        maxMp_ = std::min(200.0f, maxMp_ + 4);
        maxShadow_ = std::min(200.0f, maxShadow_ + 4);
        hp_ = hpGhost_ = maxHp_;
        levelFlash_ = 2.5f;
        notify("Nivel " + std::to_string(heroLevel_) + " · vida +10 · fuerza +1,5", rgba(190, 240, 255));
        uiSound(Sfx::PerfectGuard, 0.8f, 0.8f);
        Vec3 chest = hero_.body.pos + Vec3{0, 1.0f, 0};
        spawnSparks(chest, {0, 1, 0}, 40, rgba(190, 240, 255), 5.0f);
        spawnRing(hero_.body.pos + Vec3{0, 0.05f, 0}, 4.0f, 0.6f, rgba(190, 240, 255));
    }
    if (heroLevel_ >= kMaxLevel) xp_ = 0;
}

void Game::heal(float hp, float mp) {
    hp_ = std::min(maxHp_, hp_ + hp);
    mp_ = std::min(maxMp_, mp_ + mp);
}

bool Game::craft(const Recipe& r) {
    if (!inv_.craft(r, heroLevel_)) return false;
    const ItemDef& d = itemDef(r.result);
    notify(std::string("Fabricado: ") + d.name + (r.count > 1 ? " ×" + std::to_string(r.count) : ""), d.color);
    uiSound(Sfx::ItemGet, 0.8f, 1.1f);
    return true;
}

bool Game::useItem(ItemId id) {
    const ItemDef& d = itemDef(id);
    if (d.kind == ItemKind::Core && inv_.count(id) > 0)  // núcleo: transformarse
        return startForm(id == ItemId::CoreAurora ? Form::Aurora : id == ItemId::CoreColossus ? Form::Colossus : Form::Echo);
    if (d.kind == ItemKind::Key && id == ItemId::NoraLantern) return summon(1);
    if (d.kind == ItemKind::Upgrade && inv_.count(id) > 0) {  // afilador: fuerza permanente
        if (inv_.powerBonus >= Inventory::kMaxPowerBonus) {
            notify("El prisma ya no admite más filo", rgba(180, 180, 190));
            return false;
        }
        inv_.remove(id);
        inv_.powerBonus = std::min(Inventory::kMaxPowerBonus, inv_.powerBonus + 2.0f);
        notify("Fuerza +2 (filo " + std::to_string(int(inv_.powerBonus)) + "/6)", rgba(255, 255, 255));
        uiSound(Sfx::PerfectGuard, 0.7f);
        return true;
    }
    if (d.kind != ItemKind::Consumable || inv_.count(id) <= 0 || hero_.downTimer >= 0) return false;
    bool useful = (d.heal > 0 && hp_ < maxHp_) || (d.mana > 0 && mp_ < maxMp_);
    if (!useful) {
        notify("Ahora no hace falta", rgba(180, 180, 190));
        return false;
    }
    inv_.remove(id);
    heal(d.heal, d.mana);
    spawnRing(hero_.body.pos + Vec3{0, 0.05f, 0}, 1.3f, 0.6f, d.color);
    for (int i = 0; i < 16 && particles_.size() < kMaxParticles; ++i) {
        float a = random01() * 2.0f * kPi;
        Vec3 p = hero_.body.pos + Vec3{std::cos(a) * 0.5f, randRange(0.2f, 1.4f), std::sin(a) * 0.5f};
        particles_.push_back({p, {0, randRange(1.0f, 2.0f), 0}, 0.8f, 0.8f, 0.1f, d.color, 0, true});
    }
    return true;
}

bool Game::equip(ItemId id) {
    if (itemDef(id).kind != ItemKind::Weapon || inv_.count(id) <= 0) return false;
    inv_.weapon = id;
    applyWeapon();
    return true;
}
