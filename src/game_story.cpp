// Historia en el juego: escenas del guion (una sola vez cada una, en cola), las órdenes que
// dan, Brizna (la luz guía que acompaña al Prismero) y los ecos de memoria.

#include "game.h"

#include <algorithm>
#include <cstdlib>

void Game::playScene(const std::string& id) {
    if (!story_ || played_.count(id) || !script_.find(id)) return;
    played_.insert(id);
    sceneQueue_.push_back(id);
}

std::string Game::speakerName(const std::string& id) const {
    auto it = names_.find(id);
    return it != names_.end() ? it->second : id;
}

void Game::storyCommand(const StoryLine& cmd) {
    const std::string& c = cmd.speaker;
    if (c == "negro") {
        fade_ = 1;
    } else if (c == "levantarse") {
        standingUp_ = true;
    } else if (c == "nombre") {
        size_t sp = cmd.text.find(' ');
        if (sp != std::string::npos) names_[cmd.text.substr(0, sp)] = cmd.text.substr(sp + 1);
    } else if (c == "capitulo") {
        chapterBanner_ = 5.0f;
    } else if (c == "armar") {  // un combate que espera al guion (jefes)
        for (Encounter& e : encounters_)
            if (e.id == cmd.text) e.armed = true;
    } else if (c == "abrir") {
        setGate(cmd.text, false);
    } else if (c == "dar") {
        if (cmd.text == "farolillo") inv_.add(ItemId::BlazeGem);
    } else if (c == "eco_activa") {
        int n = std::atoi(cmd.text.c_str()) - 1;
        if (n >= 0 && n < int(echoes_.size())) {
            echoes_[size_t(n)].active = true;
            echoes_[size_t(n)].appear = 0;
            spawnRing(echoes_[size_t(n)].pos - Vec3{0, 0.8f, 0}, 1.2f, 0.8f, rgba(255, 255, 255));
        }
    } else if (c == "fin_capitulo") {
        chapterEnded_ = true;
        chapterBanner_ = 7.0f;
    }
}

void Game::updateStory(float dt) {
    fade_ = std::max(0.0f, fade_ - dt * (hero_.lying > 0.5f ? 0.35f : 0.8f));
    chapterBanner_ = std::max(0.0f, chapterBanner_ - dt);
    if (standingUp_) {
        hero_.lying = std::max(0.0f, hero_.lying - dt * 0.9f);
        if (hero_.lying <= 0) standingUp_ = false;
    }
    // Siguiente escena de la cola (un aviso no interrumpe una escena que bloquea).
    if (!dialogue_.active() && !sceneQueue_.empty()) {
        dialogue_.start(script_.find(sceneQueue_.front()));
        sceneQueue_.pop_front();
    }
    std::vector<StoryLine> commands;
    const StoryLine* before = dialogue_.line();
    dialogue_.update(dt, commands);
    for (const StoryLine& cmd : commands) storyCommand(cmd);
    // Sonido de "tecleo" mientras aparece el texto (cada voz con su tono).
    const StoryLine* l = dialogue_.line();
    if (l && l == before && dialogue_.newChars() > 0 && dialogue_.visibleChars() % 2 == 0) {
        float pitch = l->speaker == "brizna" ? 1.35f : l->speaker == "prota" ? 0.9f : l->speaker == "voz" ? 0.7f : 1.1f;
        uiSound(Sfx::Blip, 0.35f, pitch);
    }

    // Disparadores de escenas.
    if (!story_) return;
    for (const Enemy& e : enemies_) {
        if (!e.targetable()) continue;
        if (e.kind == EnemyKind::Shroom && !seenShroom_) {
            seenShroom_ = true;
            playScene("domo");
        }
        if (e.kind == EnemyKind::Moth && !seenMoth_) {
            seenMoth_ = true;
            playScene("eco_enemigo");
        }
    }

    // Ecos de memoria: se recogen al pasar cerca.
    for (MemoryEcho& m : echoes_) {
        if (!m.active || m.taken) continue;
        m.appear += dt;
        Vec3 d = m.pos - (hero_.body.pos + Vec3{0, 0.9f, 0});
        if (hero_.downTimer < 0 && length(d) < 1.1f) {
            m.taken = true;
            sound(Sfx::Echo, m.pos);
            spawnSparks(m.pos, {0, 1, 0}, 30, rgba(255, 255, 255), 3.0f);
            playScene(m.scene);
        }
    }
}

// Brizna flota junto al hombro del Prismero siguiéndole con un muelle; en las escenas se
// pone delante, a la altura de su cara.
void Game::updateBrizna(float dt) {
    Vec3 fwd = hero_.forward(), right{-fwd.z, 0, fwd.x};
    Vec3 target = hero_.body.pos + right * 0.55f + Vec3{0, 1.75f + 0.08f * std::sin(time_ * 2.3f), 0} - fwd * 0.15f;
    if (dialogue_.blocking() && !dialogue_.memory())
        target = hero_.body.pos + fwd * 0.95f + right * 0.3f + Vec3{0, hero_.lying > 0.5f ? 0.9f : 1.5f, 0};
    if (briznaPos_.x == 0 && briznaPos_.y == 0 && briznaPos_.z == 0) briznaPos_ = target;
    const float k = 28.0f, c = 8.5f;
    Vec3 acc = (target - briznaPos_) * k - briznaVel_ * c;
    briznaVel_ += acc * dt;
    briznaPos_ += briznaVel_ * dt;
    // Deja un rastro de chispas suaves.
    if (random01() < dt * 14.0f && particles_.size() < 900)
        particles_.push_back({briznaPos_, {randRange(-0.2f, 0.2f), randRange(-0.3f, 0.1f), randRange(-0.2f, 0.2f)}, 0.7f,
                              0.7f, 0.035f, rgba(255, 240, 190), 0, false});
}

bool Game::inCombat() const {
    for (const Enemy& e : enemies_)
        if (e.alive()) return true;
    return false;
}

void Game::sound(Sfx id, Vec3 pos, float gain, float pitch) { sounds_.push_back({id, pos, true, gain, pitch}); }
void Game::uiSound(Sfx id, float gain, float pitch) { sounds_.push_back({id, {}, false, gain, pitch}); }

void Game::loadBrains(const std::string& dir) {
    brainDir_ = dir;
    const char* names[] = {"rastrero", "domo", "eco"};
    for (int k = 0; k < int(EnemyKind::Count); ++k) brains_[k].load(dir + "/ia_" + names[k] + ".q");
    bossBrain_.load(dir + "/ia_jefe.q");
}

void Game::saveBrains() const {
    if (brainDir_.empty()) return;
    const char* names[] = {"rastrero", "domo", "eco"};
    for (int k = 0; k < int(EnemyKind::Count); ++k) brains_[k].save(brainDir_ + "/ia_" + names[k] + ".q");
    bossBrain_.save(brainDir_ + "/ia_jefe.q");
}
