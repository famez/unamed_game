// Grises salvajes de los mapas abiertos: aparecen al azar por la zona, fuera de la vista
// cercana (entre 26 y 42 m de la protagonista, en suelo libre y seco y lejos de los pueblos),
// en grupos pequeños; como mucho unos pocos a la vez. Los que se quedan muy atrás desaparecen
// y los disipados se retiran de la lista al terminar de deshacerse.

#include "game/game.h"

#include <algorithm>
#include <cmath>

namespace {

constexpr int kMaxWild = 5;
constexpr float kNear = 26, kFar = 42;  // dónde aparecen (distancia a la protagonista)
constexpr float kForget = 120;          // más lejos, desaparecen

}  // namespace

void Game::compactEnemies() {
    std::vector<int> remap(enemies_.size(), -1);
    std::vector<Enemy> kept;
    for (size_t i = 0; i < enemies_.size(); ++i) {
        const Enemy& e = enemies_[i];
        bool done = e.state == AiState::Dying && e.stateTime > 1.5f;
        bool far = e.alive() && length(flat(e.body.pos - hero_.body.pos)) > kForget;
        if (done || far) continue;
        remap[i] = int(kept.size());
        kept.push_back(e);
    }
    if (kept.size() == enemies_.size()) return;
    auto fix = [&](int& idx) { idx = idx >= 0 && idx < int(remap.size()) ? remap[size_t(idx)] : -1; };
    fix(lock_);
    fix(riposteTarget_);
    for (Projectile& p : projectiles_) {
        fix(p.target);
        p.hitMask = 0;
    }
    enemies_ = std::move(kept);
}

void Game::updateWild(float dt) {
    if (!wildSpawns_) return;
    compactEnemies();
    int alive = 0, swarm = 0;
    for (const Enemy& e : enemies_) (isSwarm(e.kind) ? swarm : alive) += e.alive();
    wildTimer_ -= dt;
    if (wildTimer_ > 0 || hero_.downTimer >= 0 || inLight_ || leaving_) return;
    // Qué sale: Deslumbrados y Vigías (luz), Sabuesos en manada o un Coloso (sombra), cangrejos
    // de cristal en la orilla, o una nube de polillas de tinta que va a por las farolas (en la
    // ciudad, solo polillas).
    float r = random01();
    bool moths = wildOnlyMoths_ || r < 0.2f;
    // Con agua cerca (el río del campo, la bahía), a veces cangrejos de cristal en la orilla.
    bool crabs = !moths && waterLevel_ > -100 && r < 0.35f;
    if (moths ? swarm > 0 : alive >= kMaxWild) return;
    wildTimer_ = wildOnlyMoths_ ? randRange(25.0f, 40.0f) : randRange(7.0f, 14.0f);
    r = random01();
    EnemyKind kind = moths ? EnemyKind::Moth
                   : crabs    ? EnemyKind::Crab
                   : r < 0.35f ? EnemyKind::Dazzled : r < 0.55f ? EnemyKind::Watcher : r < 0.88f ? EnemyKind::Hound : EnemyKind::Brute;
    int count = moths ? 8 + int(random01() * 5.0f)
              : kind == EnemyKind::Hound || kind == EnemyKind::Crab ? 2 + int(random01() * 2.0f) : kind == EnemyKind::Brute ? 1 : 1 + int(random01() * 2.0f);
    if (!moths) count = std::min(count, kMaxWild - alive);
    for (int tries = 0; tries < 16; ++tries) {
        float a = random01() * 2 * kPi, d = randRange(kNear, kFar);
        Vec3 c = hero_.body.pos + Vec3{std::cos(a) * d, 0, std::sin(a) * d};
        c.y = world_.groundAt(c.x, c.z);
        bool ok = (crabs ? c.y > waterLevel_ + 0.05f && c.y < waterLevel_ + 1.2f : c.y > waterLevel_ + 0.4f) &&
                  std::fabs(c.y - hero_.body.pos.y) < 14.0f;
        for (const Vec3& safe : wildSafe_) ok &= length(flat(c - safe)) > 40.0f;
        if (!ok || !devFreeSpot(c, 1.0f) || length(flat(c - hero_.body.pos)) < kNear * 0.8f) continue;
        for (int k = 0; k < count; ++k) {
            Vec3 p = c + Vec3{std::cos(float(k) * 2.1f) * 1.8f, 0, std::sin(float(k) * 2.1f) * 1.8f} * float(k > 0);
            p.y = world_.groundAt(p.x, p.z) + (moths ? 2.0f + random01() : 0.0f);
            spawnEnemy({kind, p});
        }
        return;
    }
}
