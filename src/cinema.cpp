// La historia sin palabras: planos de cámara, ecos (siluetas de luz que repiten un recuerdo),
// Ilse, tutoriales de una tecla y una palabra, y los poderes que la historia va devolviendo.
//
// Órdenes del guion que se atienden aquí:
//   !plano fx fy fz ex ey ez segundos   la cámara mira a (f) desde (e)
//   !eco nombre                         aparecen los ecos de ese recuerdo
//   !ilse x y z | !ilse_gesto | !ilse_fuera
//   !tutorial tecla acción palabra      p. ej. «!tutorial Q absorber Absorber»
//   !poder nombre                       fuerza, ataque, absorber, luz, tecnicas, supernova, sombra
//   !apagar_luces                       todas las luces del escenario se apagan

#include "game.h"

#include <algorithm>
#include <sstream>

namespace {
const uint32_t kGhostLight = rgba(255, 232, 196);

Vec3 parseVec(std::istringstream& in) {
    Vec3 v;
    in >> v.x >> v.y >> v.z;
    return v;
}
}  // namespace

void Game::jumpToScene(const std::string& id) {
    if (!story_) return;
    LevelId lv = LevelId::Plaza;
    for (const char* m : {"mercado", "calle", "nora", "eco_nora"})
        if (id.find(m) == 0) lv = LevelId::Mercado;
    for (const char* p : {"puente", "eco_teo", "archivera", "fin"})
        if (id.find(p) == 0) lv = LevelId::Puente;
    dialogue_.stop();
    sceneQueue_.clear();
    shots_.clear();
    tutorials_.clear();
    played_.insert("intro");
    if (lv != level_) loadLevel(lv);
    dialogue_.stop();
    sceneQueue_.clear();
    hero_.lying = 0;
    fade_ = 0;
    powers_ = Powers{};
    played_.erase(id);
    playScene(id);
}

bool Game::cinematic(Vec3& eye, Vec3& focus) const {
    if (shots_.empty()) return false;
    eye = shots_.front().eye;
    focus = shots_.front().focus;
    // Un leve avance de cámara durante el plano (que no sea una foto fija).
    float u = clampf(shotTime_ / std::max(0.1f, shots_.front().duration), 0, 1);
    eye = eye + (focus - eye) * (0.08f * u);
    return true;
}

// Sin el poder, la acción no existe todavía.
Controls Game::filterControls(const Controls& in) const {
    Controls c = in;
    const Powers& p = powers_;
    if (!p.attack) c.attack = c.attackHeld = c.guard = false;
    if (!p.jump) c.jump = false;
    if (!p.dodge) c.dodge = false;
    if (!p.sprint) c.sprint = false;
    if (!shadowMode_) {
        if (!p.absorb) c.absorb = false;
        if (!p.light) c.light = false;
        if (!p.nova) c.nova = false;
    }
    if (!p.techniques) {
        if (c.command >= 0 && c.command < 3) c.command = -1;
        if (commandSel_ < 3) c.runCommand = false;
        c.summon = false;
    }
    return c;
}

void Game::spawnEcho(const std::string& name) {
    auto ghost = [&](Vec3 pos, float yaw, std::vector<GhostAct> acts, bool loop = false) {
        Ghost g;
        g.body.body.pos = pos;
        g.body.yaw = yaw;
        g.body.update(1.0f / 60.0f, {}, false, world_);
        g.acts = std::move(acts);
        g.loop = loop;
        ghosts_.push_back(g);
        spawnSparks(pos + Vec3{0, 1, 0}, {0, 1, 0}, 16, kGhostLight, 2.0f);
    };
    using A = GhostAct;
    if (name == "alba_farola") {  // una mujer arregla una farola; un chico le sujeta la escalera
        Vec3 lamp{-5.5f, 0, -5.5f};
        ghost(lamp + Vec3{0.6f, 0, 0.2f}, -kPi * 0.5f,
              {{A::Wait, {}, 1.0f}, {A::Gesture}, {A::Wait, {}, 1.2f}, {A::Gesture}, {A::Wait, {}, 1.5f},
               {A::Face, lamp + Vec3{2.0f, 0, -1.5f}}, {A::Wait, {}, 2.5f}, {A::Vanish}});
        ghost(lamp + Vec3{2.0f, 0, -1.5f}, kPi * 0.7f,
              {{A::Wait, {}, 4.5f}, {A::Walk, lamp + Vec3{1.4f, 0, -0.6f}}, {A::Wait, {}, 1.5f}, {A::Walk, lamp + Vec3{4.5f, 0, -5.0f}},
               {A::Vanish}});
    } else if (name == "alba_mira") {  // ella, en lo alto, mira la ciudad y se vuelve
        ghost({-11.6f, 2.6f, 11.8f}, kPi * 0.25f,
              {{A::Wait, {}, 2.5f}, {A::Face, {-8.0f, 2.6f, 8.0f}}, {A::Wait, {}, 1.0f}, {A::Gesture}, {A::Wait, {}, 2.5f}, {A::Vanish}});
    } else if (name == "buscando") {  // alguien recorre la calle buscando a alguien
        ghost({-1.5f, 0, 14.0f}, 0,
              {{A::Walk, {1.2f, 0, 18.5f}}, {A::Face, {-3.0f, 0, 19.0f}}, {A::Wait, {}, 0.9f}, {A::Face, {3.0f, 0, 19.0f}},
               {A::Wait, {}, 0.9f}, {A::Gesture}, {A::Wait, {}, 1.0f}, {A::Vanish}});
    } else if (name == "vendedor") {  // alguien que repite el mismo gesto de vender, una y otra vez
        ghost({-3.6f, 0, 14.0f}, kPi * 0.5f, {{A::Gesture}, {A::Wait, {}, 1.4f}}, true);
        ghost({3.6f, 0, 18.0f}, -kPi * 0.5f, {{A::Wait, {}, 0.7f}, {A::Gesture}, {A::Wait, {}, 1.0f}}, true);
    } else if (name == "nora_lia") {  // una madre en su puesto; su hija se va y se despide
        Vec3 stall{8.5f, 0, 43.4f};
        ghost(stall, kPi, {{A::Wait, {}, 2.0f}, {A::Gesture}, {A::Wait, {}, 5.0f}, {A::Vanish}});
        ghost(stall + Vec3{-0.8f, 0, -1.0f}, 0,
              {{A::Wait, {}, 0.8f}, {A::Walk, stall + Vec3{-2.5f, 0, 4.0f}}, {A::Face, stall}, {A::Gesture}, {A::Wait, {}, 0.6f},
               {A::Walk, stall + Vec3{-3.5f, 0, 10.0f}}, {A::Vanish}});
    } else if (name == "teo_puente") {  // un chico no se atreve a cruzar; ella vuelve a por él
        ghost({0.6f, 1.0f, 52.0f}, kPi, {{A::Wait, {}, 5.0f}, {A::Walk, {0.3f, 1.0f, 57.0f}}, {A::Vanish}});
        ghost({0.2f, 1.0f, 57.5f}, kPi,
              {{A::Wait, {}, 1.0f}, {A::Walk, {0.2f, 1.0f, 53.0f}}, {A::Gesture}, {A::Wait, {}, 0.6f}, {A::Walk, {0.0f, 1.0f, 58.0f}},
               {A::Vanish}});
    }
}

bool Game::cinemaCommand(const StoryLine& cmd) {
    const std::string& c = cmd.speaker;
    std::istringstream in(cmd.text);
    if (c == "plano") {
        Shot s;
        s.focus = parseVec(in);
        s.eye = parseVec(in);
        in >> s.duration;
        if (shots_.empty()) shotTime_ = 0;
        shots_.push_back(s);
    } else if (c == "eco") {
        spawnEcho(cmd.text);
        sound(Sfx::Echo, hero_.body.pos, 0.6f, 0.8f);
    } else if (c == "ilse") {
        Ghost g;
        g.ilse = true;
        g.body.body.pos = parseVec(in);
        Vec3 to = hero_.body.pos - g.body.body.pos;
        g.body.yaw = std::atan2(to.x, to.z);
        g.body.update(1.0f / 60.0f, {}, false, world_);
        ghosts_.push_back(g);
    } else if (c == "ilse_gesto" || c == "ilse_fuera") {
        for (Ghost& g : ghosts_)
            if (g.ilse) {
                g.acts.push_back({c == "ilse_gesto" ? GhostAct::Gesture : GhostAct::Vanish});
                if (c == "ilse_gesto") sound(Sfx::WaveStart, g.body.body.pos, 0.8f, 0.7f);
            }
    } else if (c == "tutorial") {
        Tutorial t;
        in >> t.key >> t.action;
        std::getline(in, t.word);
        t.word.erase(0, t.word.find_first_not_of(' '));
        tutorials_.push_back(t);
    } else if (c == "poder") {
        const std::string& p = cmd.text;
        if (p == "fuerza") powers_.strong = powers_.jump = powers_.dodge = powers_.sprint = true;
        else if (p == "ataque") powers_.attack = true;
        else if (p == "absorber") powers_.absorb = true;
        else if (p == "luz") powers_.light = true;
        else if (p == "tecnicas") powers_.techniques = true;
        else if (p == "supernova") powers_.nova = true;
        else if (p == "sombra") shadowUnlocked_ = true;
        spawnRing(hero_.body.pos + Vec3{0, 0.05f, 0}, 2.5f, 0.6f, p == "sombra" ? rgba(150, 80, 255) : rgba(255, 226, 160));
    } else if (c == "apagar_luces") {  // la luz entera del escenario, bebida de golpe
        for (Lamp& l : lamps_)
            if (l.on && l.bulb) {
                l.on = false;
                addPool(Vec3{l.pos.x, l.pos.y - 2.0f, l.pos.z}, 5.0f);
            }
        for (Lantern& l : lanterns_) {
            if (l.lit) addPool(l.pos, 4.5f);
            l.lit = false;
        }
        croma_ = cromaShown_ = 0.0f;
        shake_ = std::max(shake_, 0.4f);
        sound(Sfx::FireBoom, hero_.body.pos, 0.6f, 0.4f);
    } else {
        return false;
    }
    return true;
}

void Game::updateCinema(float dt, const Controls& c) {
    // Planos: pasan solos; pulsar los acorta.
    if (!shots_.empty()) {
        shotTime_ += dt;
        if (shotTime_ >= shots_.front().duration || (c.advance && shotTime_ > 0.3f)) {
            shots_.pop_front();
            shotTime_ = 0;
        }
    }
    cineBars_ = moveTowards(cineBars_, shots_.empty() ? 0.0f : 1.0f, dt * 2.5f);
    // Ecos e Ilse.
    for (Ghost& g : ghosts_) {
        g.age += dt;
        if (g.fade > 0) {
            g.fade += dt;
            continue;
        }
        Vec3 move;
        if (g.act < g.acts.size()) {
            GhostAct& a = g.acts[g.act];
            bool next = false;
            switch (a.kind) {
                case GhostAct::Walk: {
                    Vec3 to = flat(a.p - g.body.body.pos);
                    if (length(to) < 0.3f) next = true;
                    else move = normalize(to) * 0.32f;  // andar despacio
                    break;
                }
                case GhostAct::Wait:
                    g.t += dt;
                    next = g.t >= a.t;
                    break;
                case GhostAct::Gesture:
                    g.body.cast(g.body.yaw);
                    next = true;
                    break;
                case GhostAct::Face: {
                    Vec3 to = flat(a.p - g.body.body.pos);
                    if (length(to) > 0.01f) g.body.yaw = std::atan2(to.x, to.z);
                    next = true;
                    break;
                }
                case GhostAct::Vanish:
                    g.fade = 0.001f;
                    spawnSparks(g.body.body.pos + Vec3{0, 1, 0}, {0, 1, 0}, 24, g.ilse ? rgba(150, 80, 255) : kGhostLight, 3.0f);
                    break;
            }
            if (next) {
                ++g.act;
                g.t = 0;
                if (g.loop && g.act >= g.acts.size()) g.act = 0;
            }
        }
        g.body.update(dt, move, false, world_);
        if (!g.ilse && random01() < dt * 12.0f && particles_.size() < 900)
            particles_.push_back({g.body.body.pos + Vec3{randRange(-0.25f, 0.25f), randRange(0.2f, 1.7f), randRange(-0.25f, 0.25f)},
                                  {0, 0.5f, 0}, 0.8f, 0.8f, 0.04f, kGhostLight, 0, true});
        if (g.loop && g.age > 14.0f && g.fade <= 0) g.fade = 0.001f;  // los que se repiten se apagan solos
    }
    ghosts_.erase(std::remove_if(ghosts_.begin(), ghosts_.end(), [](const Ghost& g) { return g.fade > 0.6f; }), ghosts_.end());

    // Tutoriales: se dan por hechos al hacer la acción.
    if (!tutorials_.empty()) {
        Tutorial& t = tutorials_.front();
        t.age += dt;
        if (t.doneTime < 0) {
            const std::string& a = t.action;
            walkTime_ = length(c.move) > 0.5f ? walkTime_ + dt : walkTime_;
            bool done = (a == "andar" && walkTime_ > 1.5f) || (a == "atacar" && c.attack) || (a == "esquivar" && c.dodge) ||
                        (a == "saltar" && c.jump) || (a == "correr" && c.sprint && length(c.move) > 0.5f) ||
                        (a == "absorber" && hero_.absorbing()) || (a == "entrar" && inLight_) ||
                        (a == "salir" && !inLight_ && !travel_ && t.age > 0.5f) ||
                        (a == "saltar_luz" && travel_ && travelMode_ == 0 && t.age > 0.5f) || (a == "cambiar" && shadowMode_) ||
                        (a == "beber" && drink_ > 0.5f) || (a == "girar" && c.interact) || (a == "supernova" && c.nova) ||
                        (a == "fijar" && lock_ >= 0) || t.age > 40.0f;
            if (done) t.doneTime = t.age;
        } else if (t.age - t.doneTime > 0.8f) {
            tutorials_.pop_front();
            walkTime_ = 0;
        }
    }
}
