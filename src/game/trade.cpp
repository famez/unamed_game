// Tiendas y teleférico.
//   - Tiendas: quien atiende tiene su género (Npc::stock); F le habla y abre la tienda, y se
//     compra con lúmenes desde el menú (InventoryMenu::showShop).
//   - Teleférico: con F en el andén se funde a negro, la protagonista aparece en la cabina que
//     espera, el cable la lleva a la otra estación (la otra cabina se cruza en sentido
//     contrario) y, con otro fundido, se baja en el andén de llegada.

#include "game/game.h"

#include <algorithm>
#include <cmath>

namespace {

constexpr float kFade = 0.45f;      // lo que tarda el negro en cerrarse o abrirse
constexpr float kWait = 0.6f;       // parada en la estación antes de salir y al llegar
constexpr float kCableSpeed = 7.0f; // m/s a media marcha
constexpr float kHang = 3.7f;       // del cable al origen de la cabina
constexpr float kFloor = 0.18f;     // el suelo de la cabina

}  // namespace

bool Game::buy(int i, std::string& message) {
    if (i < 0 || i >= int(shopStock_.size())) return false;
    const ShopItem& s = shopStock_[size_t(i)];
    if (inv_.munny < s.price) {
        message = "No te llegan los lúmenes";
        return false;
    }
    if (inv_.add(s.item) == 0) {
        message = "No te cabe más";
        return false;
    }
    inv_.munny -= s.price;
    uiSound(Sfx::ItemGet, 0.6f, 1.1f);
    message = std::string("Comprado: ") + itemName(s.item);
    return true;
}

float Game::rideDuration(const CableLine& l) const {
    return length(l.b - l.a) / kCableSpeed + 2.0f;  // con lo que tarda en arrancar y frenar
}

Vec3 Game::cabinPos(const CableLine& l, int cabin) const {
    float u = cabin == 0 ? l.t : 1.0f - l.t;
    return l.a + (l.b - l.a) * u + l.side * (cabin == 0 ? 1.0f : -1.0f) - Vec3{0, kHang, 0};
}

void Game::startRide(int line, bool forward) {
    if (ride_ >= 0 || line < 0 || line >= int(cables_.size())) return;
    const CableLine& l = cables_[size_t(line)];
    ride_ = line;
    rideForward_ = forward;
    rideT0_ = l.t;
    // La cabina que espera en la estación de salida: la 1 está en A cuando t = 0.
    rideCabin_ = (l.t < 0.5f) == forward ? 0 : 1;
    rideTime_ = 0;
    rideAboard_ = false;
    hero_.carried = true;
    hero_.carryPos = hero_.body.pos;
    lock_ = -1;
    uiSound(Sfx::Chest, 0.5f, 0.8f);
}

void Game::updateRide(float dt) {
    if (ride_ < 0) {
        rideFade_ = std::max(0.0f, rideFade_ - dt / kFade);
        return;
    }
    CableLine& l = cables_[size_t(ride_)];
    rideTime_ += dt;
    float travel = rideDuration(l);
    float leave = 2 * kFade + kWait, arrive = leave + travel, end = arrive + kWait + kFade;
    float t = rideTime_;
    if (t < kFade) {
        rideFade_ = t / kFade;
    } else {
        if (!rideAboard_) {
            rideAboard_ = true;
            cameraCut_ = true;
        }
        float u = clampf((t - leave) / travel, 0, 1);
        // Arranca y frena con suavidad.
        float s = u * u * (3 - 2 * u);
        l.t = rideT0_ + (1.0f - 2.0f * rideT0_) * s;
        rideFade_ = t < 2 * kFade ? 1.0f - (t - kFade) / kFade : t > arrive + kWait ? (t - arrive - kWait) / kFade : 0.0f;
        Vec3 dir = flat(l.b - l.a) * (rideForward_ ? 1.0f : -1.0f);
        hero_.carryPos = cabinPos(l, rideCabin_) + Vec3{0, kFloor, 0};
        if (t - dt < kFade) hero_.teleport(hero_.carryPos, std::atan2(dir.x, dir.z));  // recién subida: sin estela
    }
    if (t >= end) {  // en el andén de llegada
        l.t = 1.0f - rideT0_;
        if (rideForward_ && l.toZone != LevelId::Count) {  // la estación de arriba es otra zona: se carga
            hero_.carried = false;
            ride_ = -1;
            rideFade_ = 1;
            goToZone(l.toZone, l.toArrival, true);
            return;
        }
        Vec3 stop = rideForward_ ? l.stopB : l.stopA;
        hero_.carried = false;
        hero_.teleport({stop.x, groundHeight(stop + Vec3{0, 3, 0}), stop.z}, rideForward_ ? l.yawB : l.yawA);
        checkpoint_ = hero_.body.pos;
        checkpointYaw_ = hero_.yaw;
        cameraCut_ = true;
        rideFade_ = 1;
        ride_ = -1;
        notify(rideForward_ ? l.nameB : l.nameA, rgba(255, 220, 150));
    }
}
