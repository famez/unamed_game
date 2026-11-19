// Zonas: de una a otra por una puerta (la de la taberna, la boca de la gruta) o al llegar en el
// teleférico. Con F se funde a negro, se guarda lo hecho en la zona que se deja (cápsulas
// abiertas, nodos y faroles encendidos, farolas rotas, combates ganados, la parte y el color),
// se carga la otra (con sus piezas: PropLibrary suelta las que ya no hacen falta) y, si ya se
// había estado, se deja como estaba. Se aparece en la llegada que pide la puerta.

#include "game/game.h"

#include <algorithm>

namespace {

constexpr float kZoneFade = 0.4f;

template <class T, class F>
std::vector<char> flags(const std::vector<T>& v, F f) {
    std::vector<char> out;
    out.reserve(v.size());
    for (const T& x : v) out.push_back(char(f(x)));
    return out;
}

}  // namespace

void Game::goToZone(LevelId to, int arrival, bool black) {
    if (changingZone() || to == LevelId::Count) return;
    zoneTo_ = to;
    zoneArrival_ = arrival;
    zoneTime_ = black ? kZoneFade : 0.0f;
    if (black) zoneFade_ = 1;
    lock_ = -1;
    if (!black) uiSound(Sfx::Chest, 0.4f, 0.7f);
}

bool Game::updateZone(float dt) {
    if (!changingZone()) {
        zoneFade_ = std::max(0.0f, zoneFade_ - dt / kZoneFade);
        return false;
    }
    zoneTime_ += dt;
    zoneFade_ = std::max(zoneFade_, std::min(1.0f, zoneTime_ / kZoneFade));
    if (zoneTime_ < kZoneFade) return false;
    LevelId to = zoneTo_;
    zoneTo_ = LevelId::Count;
    enterZone(to, zoneArrival_);
    zoneFade_ = 1;  // y se abre desde negro
    return true;
}

void Game::enterZone(LevelId to, int arrival) {
    saveZoneState();
    float color = croma_;  // el color que se ha devuelto a esta parte del mundo pasa a la de al lado
    loadLevel(to);
    bool known = zoneStates_.count(int(to)) > 0;
    restoreZoneState();
    croma_ = cromaShown_ = std::max(croma_, color);
    if (arrival >= 0 && arrival < int(arrivals_.size())) {
        const ZoneArrival& a = arrivals_[size_t(arrival)];
        hero_.teleport({a.pos.x, groundHeight(a.pos + Vec3{0, 1.5f, 0}), a.pos.z}, a.yaw);
        checkpoint_ = hero_.body.pos;
        checkpointYaw_ = hero_.yaw;
    }
    cameraCut_ = true;
    if (known) partBanner_ = 0;  // a donde ya se había estado: sin el rótulo de la parte
    notify(levelName_, rgba(255, 220, 150));
}

void Game::saveZoneState() {
    if (level_ == LevelId::Count) return;
    ZoneState z;
    z.chests = flags(chests_, [](const TreasureChest& c) { return c.open; });
    z.nodes = flags(nodes_, [](const ColorNode& n) { return n.lit; });
    z.lanterns = flags(lanterns_, [](const Lantern& l) { return l.lit; });
    z.interacts = flags(interacts_, [](const Interactable& i) { return i.used; });
    z.lamps = flags(lamps_, [](const Lamp& l) { return l.broken; });
    z.encounters = flags(encounters_, [](const Encounter& e) { return e.cleared; });
    z.part = part_;
    z.croma = croma_;
    zoneStates_[int(level_)] = std::move(z);
}

void Game::restoreZoneState() {
    auto it = zoneStates_.find(int(level_));
    if (it == zoneStates_.end()) return;
    const ZoneState& z = it->second;
    // Si la zona ha cambiado de forma (otra versión del mapa), lo que no cuadre se queda como está.
    for (size_t i = 0; i < chests_.size() && i < z.chests.size(); ++i)
        if (z.chests[i]) chests_[i].open = true, chests_[i].openTime = 10;
    for (size_t i = 0; i < nodes_.size() && i < z.nodes.size(); ++i)
        if (z.nodes[i]) nodes_[i].lit = true, nodes_[i].litTime = 10;
    for (size_t i = 0; i < lanterns_.size() && i < z.lanterns.size(); ++i)
        if (z.lanterns[i]) lanterns_[i].lit = true, lanterns_[i].litTime = 10;
    for (size_t i = 0; i < interacts_.size() && i < z.interacts.size(); ++i) interacts_[i].used = z.interacts[i];
    for (size_t i = 0; i < lamps_.size() && i < z.lamps.size(); ++i)
        if (z.lamps[i]) lamps_[i].broken = true, lamps_[i].on = false, lamps_[i].hp = 0;
    for (size_t i = 0; i < encounters_.size() && i < z.encounters.size(); ++i)
        if (z.encounters[i]) encounters_[i].cleared = encounters_[i].started = true;
    part_ = std::min(z.part, parts_.empty() ? size_t(0) : parts_.size() - 1);
    croma_ = cromaShown_ = std::max(croma_, z.croma);
}

void Game::devPlaceHero(float x, float z, float yaw) {
    float y = world_.terrain ? world_.terrain->heightAt(x, z) : groundHeight({x, 200, z});  // bajo techo: el relieve
    hero_.teleport({x, y + 0.05f, z}, yaw);
    checkpoint_ = hero_.body.pos;
    checkpointYaw_ = yaw;
    cameraCut_ = true;
}
