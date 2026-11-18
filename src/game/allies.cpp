// Transformaciones y compañeros invocables.
//
// Formas (G, 50 de Poder, 20 s), una por mitad:
//   - Forma Solar (luz): el pelo se le enciende en púas de fuego dorado, un aura de llamas y
//     rayos le rodea; todo más rápido (golpes un 40 %), un 50 % más fuerte y cada remate lanza
//     una onda de luz.
//   - Lobo de sombra (sombra): se convierte en un lobo enorme de sombra; corre mucho, muerde
//     rápido (cada mordisco le devuelve vida) y apenas se inmuta cuando le golpean.
// Cambiar de mitad (Z o un portal) deshace la forma.
// Compañeros (T): una chispa aliada (cuesta Poder) o, con su farolillo, el eco de Nora.

#include "game/game.h"

#include <algorithm>

namespace {
constexpr float kSparkCost = 40.0f, kNoraCost = 30.0f, kNoraCooldown = 60.0f;
}  // namespace

const char* formName(Form f) {
    switch (f) {
        case Form::Solar: return "FORMA SOLAR";
        case Form::Wolf: return "LOBO DE SOMBRA";
        default: return "";
    }
}

uint32_t formColor(Form f) {
    switch (f) {
        case Form::Solar: return rgba(255, 200, 60);
        case Form::Wolf: return rgba(150, 70, 255);
        default: return rgba(255, 255, 255);
    }
}

float Game::formDamage() const { return form_ == Form::Solar ? 1.5f : form_ == Form::Wolf ? 1.6f : 1.0f; }

bool Game::startForm(Form f) {
    if (form_ != Form::None || hero_.downTimer >= 0 || inLight_ || travel_ || diving_) return false;
    if (mp_ < kFormCost) {
        notify("Transformarse cuesta 50 de Poder", rgba(200, 200, 215));
        return false;
    }
    mp_ -= kFormCost;
    voiceCooldown_ = 0;
    voice(VoiceLine::Finisher);
    form_ = f;
    formTime_ = kFormDuration;
    applyWeapon();
    uint32_t col = formColor(f);
    Vec3 base = hero_.body.pos;
    if (f == Form::Solar) {  // estallido dorado: columna de luz, anillos y chispas hacia arriba
        spawnRing(base + Vec3{0, 0.05f, 0}, 4.5f, 0.8f, col);
        spawnRing(base + Vec3{0, 0.06f, 0}, 2.5f, 0.6f, rgba(255, 250, 220));
        spawnSparks(base + Vec3{0, 1, 0}, {0, 1, 0}, 60, col, 9.0f);
        novaFlash_ = 0.4f;
        sound(Sfx::FireBoom, base, 1.0f, 1.3f);
    } else {  // estallido de humo negro del que sale el lobo
        wolf_ = makeEnemy(EnemyKind::Hound, base, hero_.yaw, 0.5f);
        wolf_.state = AiState::Idle;
        wolf_.size = 1.6f;
        for (int k = 0; k < 40 && particles_.size() < kMaxParticles; ++k) {
            float a = random01() * 2.0f * kPi;
            Vec3 v{std::cos(a) * randRange(1.0f, 4.0f), randRange(0.5f, 3.0f), std::sin(a) * randRange(1.0f, 4.0f)};
            particles_.push_back({base + Vec3{0, 0.6f, 0}, v, 1.0f, 1.0f, 0.25f, k % 4 ? rgba(20, 12, 30) : col, -0.5f, false});
        }
        spawnRing(base + Vec3{0, 0.05f, 0}, 3.5f, 0.6f, col);
        sound(Sfx::HitHeavy, base, 1.0f, 0.5f);
    }
    shake_ = std::max(shake_, 0.45f);
    hitStop_ = 0.1f;
    notify(std::string(formName(f)) + " · 20 s", col);
    uiSound(Sfx::Echo, 1.0f, f == Form::Solar ? 1.3f : 0.5f);
    return true;
}

void Game::endForm() {
    if (form_ == Form::Wolf)
        spawnSparks(hero_.body.pos + Vec3{0, 0.8f, 0}, {0, 1, 0}, 20, rgba(20, 12, 30), 3.0f, false);
    form_ = Form::None;
    formTime_ = 0;
    applyWeapon();
    notify("La forma se desvanece", rgba(200, 200, 215));
}

bool Game::summon(int kind) {
    if (hero_.downTimer >= 0 || companions_.size() >= 2) return false;
    if (kind == 1) {
        if (inv_.count(ItemId::NoraLantern) == 0 || noraCooldown_ > 0 || mp_ < kNoraCost) return false;
        mp_ -= kNoraCost;
        noraCooldown_ = kNoraCooldown;
    } else {
        if (mp_ < kSparkCost) return false;
        mp_ -= kSparkCost;
    }
    Companion c;
    c.kind = kind;
    c.pos = hero_.body.pos + Vec3{0, 2.0f, 0};
    c.life = kind == 1 ? 25.0f : 20.0f;
    companions_.push_back(c);
    spawnSparks(c.pos, {0, 1, 0}, 24, kind == 1 ? rgba(255, 190, 110) : rgba(120, 230, 255), 4.0f);
    notify(kind == 1 ? "Nora acude con su farolillo" : "Una chispa aliada se une a ti",
           kind == 1 ? rgba(255, 190, 110) : rgba(120, 230, 255));
    uiSound(Sfx::ItemGet, 0.8f, kind == 1 ? 0.8f : 1.3f);
    return true;
}

void Game::updateAllies(float dt, const Controls& c) {
    noraCooldown_ = std::max(0.0f, noraCooldown_ - dt);
    // Transformarse: la forma de la mitad activa (G otra vez la deshace).
    if (c.transform) {
        if (form_ != Form::None) endForm();
        else startForm(shadowMode_ ? Form::Wolf : Form::Solar);
    }
    if (form_ != Form::None) {
        formTime_ -= dt;
        Vec3 base = hero_.body.pos;
        if (form_ == Form::Solar) {
            // Aura de fuego dorado que sube y, de vez en cuando, un rayo que chisporrotea.
            for (int k = 0; k < 2 && random01() < dt * 45.0f && particles_.size() < kMaxParticles; ++k) {
                float a = random01() * 2.0f * kPi, r = randRange(0.3f, 0.6f);
                Vec3 p = base + Vec3{std::cos(a) * r, randRange(0.0f, 1.2f), std::sin(a) * r};
                particles_.push_back({p, {0, randRange(2.0f, 3.5f), 0}, 0.45f, 0.45f, randRange(0.06f, 0.12f),
                                      random01() < 0.7f ? formColor(form_) : rgba(255, 250, 220), -2.0f, false});
            }
            if (random01() < dt * 6.0f) spawnSparks(base + Vec3{randRange(-0.4f, 0.4f), randRange(0.6f, 1.8f), 0}, {0, 1, 0}, 3,
                                                   rgba(200, 240, 255), 5.0f);
        }
        if (formTime_ <= 0) endForm();
    }
    // Invocar: Nora si se tiene su farolillo (y está disponible); si no, una chispa.
    if (c.summon) {
        bool nora = inv_.count(ItemId::NoraLantern) > 0 && noraCooldown_ <= 0;
        if (!summon(nora ? 1 : 0)) {
            if (companions_.size() >= 2)
                notify("Ya tienes dos compañeros", rgba(200, 200, 215));
            else
                notify(nora ? "Hacen falta 30 de Poder" : "Hacen falta 40 de Poder para una chispa", rgba(200, 200, 215));
        }
    }

    Vec3 fwd = hero_.forward(), side{-fwd.z, 0, fwd.x};
    for (size_t i = 0; i < companions_.size(); ++i) {
        Companion& m = companions_[i];
        m.life -= dt;
        m.timer -= dt;
        // Objetivo: el Gris más cercano a menos de 11 m del Prismero.
        int target = -1;
        float bestD = 11.0f;
        for (size_t k = 0; k < enemies_.size(); ++k) {
            if (!enemies_[k].targetable()) continue;
            float d = length(flat(enemies_[k].body.pos - hero_.body.pos));
            if (d < bestD) bestD = d, target = int(k);
        }
        Vec3 goal = hero_.body.pos + side * (i == 0 ? -1.2f : 1.2f) + Vec3{0, 2.0f + 0.15f * std::sin(time_ * 2.0f + float(i)), 0};
        if (target >= 0 && m.kind == 0) {  // la chispa se acerca a su objetivo
            Vec3 e = enemies_[size_t(target)].center();
            goal = e + normalize(flat(hero_.body.pos - e)) * 3.0f + Vec3{0, 1.6f, 0};
        }
        Vec3 acc = (goal - m.pos) * 14.0f - m.vel * 5.5f;
        m.vel += acc * dt;
        m.pos += m.vel * dt;
        if (m.kind == 0 && target >= 0 && m.timer <= 0) {  // rayo de luz (un orbe aliado)
            Projectile p{ProjectileKind::EnemyOrb, m.pos, normalize(enemies_[size_t(target)].center() - m.pos) * 16.0f, 2.0f, target};
            p.reflected = true;
            projectiles_.push_back(p);
            m.timer = 1.0f;
            sound(Sfx::Ice, m.pos, 0.4f, 1.6f);
        }
        if (m.kind == 1 && m.timer <= 0) {  // pulso del farolillo: cura y aturde
            m.timer = 3.0f;
            if (length(m.pos - hero_.body.pos) < 7.0f) heal(6.0f, 0);
            for (Enemy& e : enemies_)
                if (e.targetable() && !e.boss && length(flat(e.body.pos - m.pos)) < 5.0f) {
                    e.state = AiState::Stagger;
                    e.stateTime = 0;
                    e.rlReward -= 0.2f;
                }
            spawnRing(Vec3{m.pos.x, groundHeight(m.pos) + 0.05f, m.pos.z}, 5.0f, 0.6f, rgba(255, 190, 110));
            sound(Sfx::Cure, m.pos, 0.5f);
        }
    }
    companions_.erase(std::remove_if(companions_.begin(), companions_.end(), [](const Companion& m) { return m.life <= 0; }),
                      companions_.end());
}

// El lobo: un Sabueso grande que hace lo que hace el protagonista (después de moverle).
void Game::updateWolf(float dt) {
    if (form_ != Form::Wolf) return;
    Vec3 base = hero_.body.pos;
    wolf_.prevPos = wolf_.body.pos;
    for (int k = 0; k < kMaxJoints; ++k) wolf_.prevJoints[k] = wolf_.joints[k];
    wolf_.body.pos = base;
    wolf_.body.vel = hero_.body.vel;  // de ella sale el paso de las patas
    wolf_.yaw = hero_.yaw;
    AiState want = isAttack(hero_.move()) ? AiState::Attack : hero_.speed > 0.5f ? AiState::Chase : AiState::Idle;
    if (want != wolf_.state) wolf_.stateTime = 0;
    wolf_.state = want;
    wolf_.stateTime = want == AiState::Attack ? hero_.moveU() * wolf_.def().attackTime : wolf_.stateTime + dt;
    wolf_.phase += hero_.speed * dt * 1.6f;
    animateEnemy(wolf_, time_, dt);
    if (random01() < dt * 25.0f && particles_.size() < kMaxParticles)  // humo de sombra que se le despega
        particles_.push_back({base + Vec3{randRange(-0.5f, 0.5f), randRange(0.3f, 1.2f), randRange(-0.5f, 0.5f)},
                              {0, 0.8f, 0}, 0.8f, 0.8f, 0.14f, random01() < 0.8f ? rgba(20, 12, 30) : formColor(form_), 0,
                              false});
}
