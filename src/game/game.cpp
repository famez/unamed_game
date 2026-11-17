#include "game/game.h"

#include <algorithm>

namespace {

constexpr float kHeroRespawn = 3.0f;

}  // namespace

float Game::random01() {
    rng_ = rng_ * 1664525u + 1013904223u;
    return float(rng_ >> 8) / float(1u << 24);
}

void Game::rebuildSolids() {
    world_.solids.clear();
    for (const Block& b : blocks_) world_.solids.push_back(b.box);
    for (const Crate& c : crates_)
        if (!c.broken) world_.solids.push_back(c.box);
    world_.solids.insert(world_.solids.end(), propSolids_.begin(), propSolids_.end());
    for (const TreasureChest& c : chests_) world_.solids.push_back(boxFromCenter(c.pos + Vec3{0, 0.35f, 0}, {0.45f, 0.35f, 0.45f}));
    // La cámara choca con lo opaco, no con las barreras de energía (son translúcidas: si la
    // cámara se apartara de ellas, saltaría de un fotograma a otro al moverse cerca).
    world_.buildGrid();
    cameraWorld_ = world_;
    for (const Gate& g : gates_)
        if (g.closed) world_.solids.push_back(g.box);
    world_.buildGrid();
}

void Game::init(const char* assetDir) {
    // Historia: el guion del capítulo (sin él, el escenario no tiene combates ni escenas).
    names_ = {{"voz", ""}, {"objeto", ""}, {"ilse", "???"}};
    story_ = assetDir && script_.load(std::string(assetDir) + "/story/capitulo1.txt");
    // Con historia se empieza sin nada: los poderes se recuperan por el camino.
    if (story_) powers_ = Powers{false, false, false, false, false, false, false, false};
    bossBrain_.alpha = 0.45f;  // el jefe aprende muy deprisa durante el combate
    bossBrain_.epsilon = 0.1f;
    loadLevel(LevelId::Plaza);
    if (story_) {
        hero_.body.pos = {0, 0, -3.4f};  // de pie junto a la fuente
        hero_.yaw = kPi;
        playScene("inicio");
    }
    inv_.add(ItemId::WoodenSword);
    inv_.add(ItemId::ShadowHammer);
    inv_.shadowWeapon = ItemId::ShadowHammer;
    inv_.add(ItemId::Potion, 3);
    inv_.add(ItemId::Ether, 1);
    applyWeapon();
}

float Game::formSpeed() const { return form_ == Form::Solar ? 1.3f : form_ == Form::Wolf ? 1.5f : 1.0f; }

void Game::applyWeapon() {
    const WeaponStats& w = inv_.weaponStats();
    hero_.comboHits = w.comboHits;
    hero_.attackSpeed = w.speed * (form_ == Form::Solar ? 1.4f : 1.0f);
    hero_.speedScale = formSpeed();
    hero_.gravityScale = 1.0f;
    hero_.infiniteJumps = false;
    hero_.moveSet = w.style == WeaponStyle::Lance                                 ? MoveSet::Lance
                    : w.style == WeaponStyle::Fists || w.style == WeaponStyle::Claws ? MoveSet::Fists
                    : w.style == WeaponStyle::Hammer                                ? MoveSet::Hammer
                                                                                    : MoveSet::Blade;
    if (form_ == Form::Wolf) {  // el lobo muerde: golpes cortos y rápidos, y un zarpazo de remate
        hero_.moveSet = MoveSet::Fists;
        hero_.comboHits = 4;
        hero_.attackSpeed = 1.5f;
    }
}

// --- Bucle principal ---------------------------------------------------------------------

void Game::update(float dt, const Controls& input) {
    const Controls c = filterControls(input);  // lo que aún no ha recuperado, no existe
    time_ += dt;
    updateStory(dt);
    updateCinema(dt, c);
    // Escena que bloquea: el juego se detiene (salvo animaciones y efectos) hasta leerla.
    if (dialogueBlocking()) {
        if (c.advance) dialogue_.advance();
        hero_.setGuard(false);
        hero_.update(dt, {}, false, world_);
        updateBrizna(dt);
        updateEffects(dt);
        return;
    }
    heroFlash_ = std::max(0.0f, heroFlash_ - dt);
    swapCooldown_ = std::max(0.0f, swapCooldown_ - dt);
    modeFlash_ = std::max(0.0f, modeFlash_ - dt);
    levelFlash_ = std::max(0.0f, levelFlash_ - dt);
    {
        float want = shadowUnlocked_ && !daylight_ ? (shadowMode_ ? 1.0f : 0.8f) * darkScale_ : 0.0f;  // de día la sombra es la del sol
        darkShown_ = moveTowards(darkShown_, want, 1.5f * dt);
    }
    lightHint_ = std::max(0.0f, lightHint_ - dt);
    updatePortals(dt);
    updateLamps(dt);
    guardFlash_ = std::max(0.0f, guardFlash_ - dt);
    heroInvuln_ = std::max(0.0f, heroInvuln_ - dt);
    shake_ = std::max(0.0f, shake_ - dt * 1.5f);
    adaptNotice_ = std::max(0.0f, adaptNotice_ - dt);
    fovKick_ += ((hero_.dashAmount() > 0 ? 0.08f : 0.0f) + (isFinisher(hero_.move()) ? 0.06f : 0.0f) - fovKick_) *
                (1.0f - std::exp(-8.0f * dt));
    lastHitAge_ += dt;
    hpGhost_ = hpGhost_ > hp_ ? (heroFlash_ > 0 ? hpGhost_ : moveTowards(hpGhost_, hp_, 40.0f * dt)) : hp_;

    bool down = hero_.downTimer >= 0;
    if (down) {
        hero_.downTimer += dt;
        if (hero_.downTimer > kHeroRespawn) respawnHero();
    } else if (inLight_ || travel_) {  // hecho luz: solo se elige fuente, se salta o se sale
        updateLightTarget(c);
        if (inLight_) updateInsideLight(dt, c);
        lock_ = -1;
        if (c.advance && dialogue_.active()) dialogue_.advance();
    } else if (diving_) {  // fundido con la sombra: solo se mueve, sale o embosca
        updateShadow(dt, c);
        lock_ = -1;
        if (c.advance && dialogue_.active()) dialogue_.advance();
    } else {
        if (c.swapMode && sandbox()) swapMode();  // en la historia se cambia con los portales
        updateLightTarget(c);
        if (shadowMode_) updateShadow(dt, c);
        else updateLight(dt, c);
        updateAllies(dt, c);
        // R fija al Gris más centrado y, si ya hay uno fijado, lo suelta; el clic central
        // pasa al siguiente.
        if (c.lockOn) {
            if (lock_ >= 0) lock_ = -1;
            else cycleLock(c.camForward);
        }
        if (c.nextTarget) cycleLock(c.camForward);
        if (c.unlock) lock_ = -1;
        if (c.commandScroll != 0)
            commandSel_ = ((commandSel_ + c.commandScroll) % kCommandCount + kCommandCount) % kCommandCount;
        if (c.command >= 0 && c.command < kCommandCount) runCommand(c.command);
        else if (c.runCommand) runCommand(commandSel_);
        hero_.setGuard(c.guard);
        if (c.advance && dialogue_.active()) dialogue_.advance();
        if (c.dodge) dodgeBuffer_ = 0.15f;
        if (c.attack) attackBuffer_ = 0.25f;
        if (c.dodge || c.attack) bufferedMove_ = c.move;
        if (dodgeBuffer_ > 0 && hero_.dodge(length(bufferedMove_) > 0.1f ? bufferedMove_ : hero_.forward())) {
            sound(Sfx::Dodge, hero_.body.pos);
            voice(VoiceLine::Dodge, 0.5f);
            dodgeBuffer_ = 0;
            attackBuffer_ = 0;
        }
        updateRiposte(dt);
        if (attackBuffer_ > 0 && !hero_.armed()) {  // atacar con el arma envainada la saca de golpe
            hero_.setArmed(true, true);
            combatTimer_ = 5.0f;
        }
        if (attackBuffer_ > 0 && heroAttack(length(c.move) > 0.1f ? c.move : bufferedMove_)) attackBuffer_ = 0;
        attackBuffer_ = std::max(0.0f, attackBuffer_ - dt);
        dodgeBuffer_ = std::max(0.0f, dodgeBuffer_ - dt);
        if (c.interact) tryInteract();
    }
    if (lock_ >= int(enemies_.size()) || (lock_ >= 0 && !enemies_[size_t(lock_)].targetable())) lock_ = -1;
    // El arma: en la mano si hay peligro cerca (o acaba de pelear); si no, a la espalda.
    {
        bool danger = lock_ >= 0 || isAttack(hero_.move());
        for (const Enemy& e : enemies_)
            danger = danger || (e.targetable() && length(flat(e.body.pos - hero_.body.pos)) < 16.0f);
        combatTimer_ = danger ? 5.0f : std::max(0.0f, combatTimer_ - dt);
        hero_.setArmed(powers_.attack && combatTimer_ > 0);
        bool alert = lock_ >= 0;  // en guardia: con un objetivo fijado o un Gris muy cerca
        for (const Enemy& e : enemies_)
            alert = alert || (e.targetable() && length(flat(e.body.pos - hero_.body.pos)) < 6.0f);
        hero_.setAlert(alert);
    }

    // Parón al golpear: todo se congela un instante salvo los efectos.
    if (hitStop_ > 0) {
        hitStop_ -= dt;
        updateEffects(dt);
        return;
    }

    bool wasGrounded = hero_.body.grounded;
    hero_.setSprint(c.sprint);
    hero_.setAttackHeld(c.attackHeld);
    bool light = inLight_ || travel_;
    // Fundido con la sombra: dentro de un charco, en modo sombra, corre más.
    hero_.speedScale = formSpeed() * (diving_ ? 1.7f : 1.0f) * (powers_.strong ? 1.0f : 0.4f);  // sin fuerzas, apenas camina
    if (diving_ && !onWall_) tryWallClimb(c);
    if (diving_ && onWall_)
        updateWallClimb(dt, c);
    else
        hero_.update(dt, down || light ? Vec3{} : c.move, !down && !light && !diving_ && c.jump, world_,
                     std::atan2(c.camForward.x, c.camForward.z));
    updateWolf(dt);
    if (c.jump && wasGrounded && hero_.body.vel.y > 1.0f) {
        sound(Sfx::Jump, hero_.body.pos, 0.7f);
        voice(VoiceLine::Jump, 0.45f);
    }
    updateTravel(dt);
    constrainDive();
    updateBrizna(dt);
    Move impact = hero_.takeImpact();
    if (impact != Move::None) resolveImpact(impact);

    // Estela del arma mientras golpea.
    for (TrailSample& t : trail_) t.age += dt;
    trail_.erase(std::remove_if(trail_.begin(), trail_.end(), [](const TrailSample& t) { return t.age > kTrailLife; }),
                 trail_.end());
    if (isAttack(hero_.move())) {
        float len = inv_.weaponStats().length;
        const Mat4& w = hero_.joints[WristR];
        trail_.push_back({transformPoint(w, kBladeBase + Vec3{0, 0, len * 0.3f}), bladeTip(), 0});
    }

    // Los enemigos de tierra son sólidos para el jugador (y entre ellos).
    for (Enemy& e : enemies_) {
        if (!e.targetable() || e.def().flying) continue;
        Body& h = hero_.body;
        bool overlapY = h.pos.y < e.body.pos.y + e.body.height && h.pos.y + h.height > e.body.pos.y;
        Vec3 d = flat(h.pos - e.body.pos);
        float dist = length(d), minDist = h.radius + e.body.radius;
        if (overlapY && dist < minDist) {
            Vec3 n = dist > 1e-4f ? d * (1.0f / dist) : Vec3{1, 0, 0};
            h.pos += n * ((minDist - dist) * 0.7f);
            e.body.pos -= n * ((minDist - dist) * 0.3f);
        }
    }
    for (size_t i = 0; i < enemies_.size(); ++i)
        for (size_t j = i + 1; j < enemies_.size(); ++j) {
            Enemy &a = enemies_[i], &b = enemies_[j];
            if (!a.alive() || !b.alive() || a.def().flying != b.def().flying) continue;
            Vec3 d = flat(a.body.pos - b.body.pos);
            float dist = length(d), minDist = a.body.radius + b.body.radius;
            if (dist >= minDist || std::fabs(a.body.pos.y - b.body.pos.y) > 1.0f) continue;
            Vec3 n = dist > 1e-4f ? d * (1.0f / dist) : Vec3{1, 0, 0};
            a.body.pos += n * ((minDist - dist) * 0.5f);
            b.body.pos -= n * ((minDist - dist) * 0.5f);
        }

    // Por índice: un jefe puede añadir enemigos mientras se recorren (los nuevos se mueven ya).
    for (const Spawn& s : deferredSpawns_) spawnEnemy(s);  // las luciérnagas de las farolas rotas
    deferredSpawns_.clear();
    for (size_t i = 0; i < enemies_.size(); ++i) updateEnemy(enemies_[i], dt);
    // Los que han terminado de desvanecerse se quitan (manteniendo el índice del fijado).
    {
        size_t out = 0;
        int newLock = -1;
        for (size_t i = 0; i < enemies_.size(); ++i) {
            if (enemies_[i].state == AiState::Dying && enemies_[i].stateTime > 0.5f) continue;
            if (int(i) == lock_) newLock = int(out);
            if (out != i) enemies_[out] = enemies_[i];
            ++out;
        }
        enemies_.resize(out);
        lock_ = newLock;
    }

    updateProjectiles(dt);
    updateBlackHoles(dt);
    updatePickups(dt);
    for (Crate& cr : crates_) cr.hitFlash = std::max(0.0f, cr.hitFlash - dt);
    for (TreasureChest& ch : chests_)
        if (ch.open) ch.openTime += dt;

    // Grito al empezar cada golpe: los remates y especiales siempre; los normales, a veces.
    voiceCooldown_ = std::max(0.0f, voiceCooldown_ - dt);
    {
        Move mv = hero_.move();
        if (mv != voicedMove_ && mv != Move::None) {
            if (isFinisher(mv) || mv == Move::Rush || mv == Move::Counter || mv == Move::Launcher)
                voice(VoiceLine::Finisher);
            else
                voice(VoiceLine::Attack, mv == Move::Cast ? 0.8f : 0.6f);
        }
        voicedMove_ = mv;
    }
    cromaShown_ = moveTowards(cromaShown_, croma_, 0.25f * dt);
    for (ColorSplash& sp : splashes_) sp.age += dt;
    updateLevel(dt);
    updateNpcs(dt);
    updateWild(dt);
    updateEffects(dt);
    if (sandbox()) {  // sin historia: lo que esté marcado como infinito
        if (infHp_) hp_ = maxHp_;
        if (infPower_) mp_ = maxMp_;
    }
}

// --- Combate del protagonista -------------------------------------------------------------

// --- Enemigos ------------------------------------------------------------------------

// --- Proyectiles (magias y orbes enemigos) -------------------------------------------------

// --- Botín ----------------------------------------------------------------------------

void Game::breakCrate(Crate& c) {
    c.broken = true;
    sound(Sfx::Crate, c.box.center());
    rebuildSolids();
    Vec3 p = c.box.center();
    for (int i = 0; i < 18 && particles_.size() < kMaxParticles; ++i) {
        Vec3 v{randRange(-3, 3), randRange(2, 5), randRange(-3, 3)};
        particles_.push_back({p, v, 0.8f, 0.8f, 0.07f, i % 2 ? rgba(200, 150, 90) : rgba(150, 100, 60), 12.0f, false});
    }
    dropLoot(p, 2 + int(random01() * 4.0f), random01() < 0.5f ? 1 : 0, random01() < 0.25f ? 1 : 0, ItemId::Potion,
             random01() < 0.1f);
}

// --- Efectos ----------------------------------------------------------------------------

void Game::notify(const std::string& text, uint32_t color) {
    notices_.push_back({text, color, 0});
    if (notices_.size() > 5) notices_.erase(notices_.begin());
}

void Game::spawnSparks(Vec3 pos, Vec3 dir, int count, uint32_t color, float speed, bool stars) {
    for (int i = 0; i < count && particles_.size() < kMaxParticles; ++i) {
        Vec3 rnd{random01() - 0.5f, random01() - 0.3f, random01() - 0.5f};
        float life = randRange(0.25f, 0.55f);
        particles_.push_back({pos, dir * (speed * 0.5f) + rnd * (speed * 1.3f), life, life,
                              stars ? randRange(0.06f, 0.14f) : randRange(0.04f, 0.08f), color, stars ? 3.0f : 12.0f,
                              stars});
    }
}

void Game::spawnRing(Vec3 pos, float radius, float life, uint32_t color) { rings_.push_back({pos, 0, life, radius, color}); }

float Game::groundHeight(Vec3 p) const {
    RayHit h;
    float floor = world_.groundAt(p.x, p.z);  // 0 o el relieve del terreno
    Vec3 from{p.x, p.y, p.z}, to{p.x, floor - 1.0f, p.z};
    if (from.y <= floor) return floor;
    return segmentVsWorld(from, to, world_, h) ? from.y + (to.y - from.y) * h.t : floor;
}

void Game::updateEffects(float dt) {
    for (Particle& p : particles_) {
        p.vel.y -= p.gravity * dt;
        p.vel = p.vel * std::exp(-1.5f * dt);
        p.pos += p.vel * dt;
        p.life -= dt;
    }
    particles_.erase(std::remove_if(particles_.begin(), particles_.end(), [](const Particle& p) { return p.life <= 0; }),
                     particles_.end());
    // Surtidor de la fuente (la de los escenarios de la ciudad parada, en el centro).
    if (!world_.terrain && level_ != LevelId::Neon && random01() < dt * 40.0f && particles_.size() < kMaxParticles) {
        float a = random01() * 2.0f * kPi, r = randRange(1.2f, 2.0f);
        particles_.push_back({{0, 2.3f, 0}, {std::cos(a) * r, randRange(2.5f, 3.5f), std::sin(a) * r}, 0.9f, 0.9f,
                              0.05f, rgba(170, 225, 255), 9.8f, false});
    }
    for (Ring& r : rings_) r.age += dt;
    rings_.erase(std::remove_if(rings_.begin(), rings_.end(), [](const Ring& r) { return r.age > r.life; }), rings_.end());
    for (Blast& b : blasts_) b.age += dt;
    blasts_.erase(std::remove_if(blasts_.begin(), blasts_.end(), [](const Blast& b) { return b.age > 0.6f; }),
                  blasts_.end());
    for (Notice& n : notices_) n.age += dt;
    notices_.erase(std::remove_if(notices_.begin(), notices_.end(), [](const Notice& n) { return n.age > 4.0f; }),
                   notices_.end());
}
