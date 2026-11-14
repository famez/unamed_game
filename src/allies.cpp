// Transformaciones (formas prismáticas) y compañeros invocables.
//
// Formas (G): consumen un núcleo fabricado en el taller con los restos de cada especie de
// Gris y duran 25 s.
//   - Aurora (Rastreros): todo más rápido y golpes un 30 % más fuertes.
//   - Coloso (Domos): golpes enormes con onda de choque; los golpes recibidos no le frenan.
//   - Eco (Ecos): casi sin gravedad, saltos sin límite y cada golpe lanza un orbe de luz.
// Compañeros (T): una chispa aliada (cuesta Luz) o, con su farolillo, el eco de Nora.

#include "game.h"

#include <algorithm>

namespace {
constexpr float kFormDuration = 25.0f;
constexpr float kSparkCost = 40.0f, kNoraCost = 30.0f, kNoraCooldown = 60.0f;
}  // namespace

const char* formName(Form f) {
    switch (f) {
        case Form::Aurora: return "FORMA AURORA";
        case Form::Colossus: return "FORMA COLOSO";
        case Form::Echo: return "FORMA ECO";
        default: return "";
    }
}

uint32_t formColor(Form f) {
    switch (f) {
        case Form::Aurora: return rgba(255, 210, 90);
        case Form::Colossus: return rgba(255, 140, 60);
        case Form::Echo: return rgba(120, 230, 255);
        default: return rgba(255, 255, 255);
    }
}

float Game::formDamage() const { return form_ == Form::Aurora ? 1.3f : form_ == Form::Colossus ? 1.8f : 1.0f; }

bool Game::startForm(Form f) {
    if (form_ != Form::None || hero_.downTimer >= 0) return false;
    ItemId core = f == Form::Aurora ? ItemId::CoreAurora : f == Form::Colossus ? ItemId::CoreColossus : ItemId::CoreEcho;
    if (!inv_.remove(core)) return false;
    form_ = f;
    formTime_ = kFormDuration;
    applyWeapon();
    uint32_t col = formColor(f);
    spawnRing(hero_.body.pos + Vec3{0, 0.05f, 0}, 3.0f, 0.7f, col);
    spawnSparks(hero_.body.pos + Vec3{0, 1, 0}, {0, 1, 0}, 40, col, 6.0f);
    shake_ = std::max(shake_, 0.3f);
    notify(std::string(formName(f)) + " · 25 s", col);
    uiSound(Sfx::Echo, 1.0f, 1.2f);
    return true;
}

void Game::endForm() {
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
    // Transformarse: el primer núcleo que se tenga (Aurora, Coloso, Eco).
    if (c.transform) {
        if (form_ != Form::None) {
            notify("Ya estás transformado", rgba(200, 200, 215));
        } else if (!startForm(Form::Aurora) && !startForm(Form::Colossus) && !startForm(Form::Echo)) {
            notify("Necesitas un núcleo (se fabrica en el taller)", rgba(200, 200, 215));
        }
    }
    if (form_ != Form::None) {
        formTime_ -= dt;
        if (random01() < dt * 30.0f && particles_.size() < 900) {  // aura
            float a = random01() * 2.0f * kPi;
            Vec3 p = hero_.body.pos + Vec3{std::cos(a) * 0.5f, randRange(0.1f, 1.8f), std::sin(a) * 0.5f};
            particles_.push_back({p, {0, randRange(0.8f, 1.8f), 0}, 0.5f, 0.5f, 0.06f, formColor(form_), 0, true});
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
                notify(nora ? "Hacen falta 30 de Luz" : "Hacen falta 40 de Luz para una chispa", rgba(200, 200, 215));
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
