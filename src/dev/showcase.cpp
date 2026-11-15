#include "dev/showcase.h"

#include <cmath>

namespace {

struct Act {
    const char* title;
    const char* subtitle;
    int steps;  // duración a 60 Hz
};

const Act kActs[] = {
    {"MODO LUZ", "Espada de luz: combo de tres tajos y remate", 480},
    {"TÉCNICAS DE LUZ", "1 Lanza de luz · 2 Refracción · 4 Estallido solar", 500},
    {"SUPERNOVA", "Técnica 5: gasta todo el Poder", 320},
    {"FORMA SOLAR", "G · 50 de Poder: más rápida, más fuerte y ondas en los remates", 560},
    {"VIAJE POR LA LUZ", "Q junto a una farola para entrar · R elige otra fuente · Q salta · Espacio sale", 640},
    {"ROMPER FAROLAS", "A golpes: la farola rota no vuelve a encenderse y deja sombra", 360},
    {"INTERRUPTORES", "F: apagar y encender un grupo de farolas", 600},
    {"MODO SOMBRA", "Z: cambio de luz a sombra", 240},
    {"MAZO DE SOMBRA", "Mazazo, barrido y salto que agrieta el suelo", 480},
    {"TÉCNICAS DE SOMBRA", "1 Púas · 2 Cadenas del vacío · 3 Drenar · 4 Mina oscura", 620},
    {"AGUJERO NEGRO", "Técnica 5 de la sombra", 360},
    {"LOBO DE SOMBRA", "G · 50 de Poder: corre, muerde y roba vida", 560},
    {"FUNDIRSE CON LA SOMBRA", "Q en la oscuridad · la luz no deja pasar · clic: emboscada desde el suelo", 660},
    {"TREPAR COMO SOMBRA", "Fundido, empuja contra una pared oscura: sube por ella y pasa al tejado", 480},
    {"BLOQUEO PERFECTO", "Guardia justo antes del golpe: aturdido · ataca: súper contraataque (luz)", 600},
    {"BLOQUEO PERFECTO", "En sombra, con el mazo: súper contraataque y corona de púas", 600},
};
constexpr int kActCount = int(sizeof(kActs) / sizeof(kActs[0]));

float yawTo(Vec3 from, Vec3 to) {
    Vec3 d = flat(to - from);
    return std::atan2(d.x, d.z);
}

// Combate sencillo: fija al Gris más cercano, se acerca y golpea.
void fight(Game& g, Controls& c, int t, int every = 11) {
    Vec3 foe;
    if (!g.lockTarget(foe)) {
        c.lockOn = t % 15 == 0;
        return;
    }
    Vec3 to = flat(foe - g.hero().body.pos);
    if (length(to) > 2.0f) c.move = normalize(to);
    c.attack = t % every == 0 && length(to) < 4.5f;
}

void spawnRing(Game& g, Vec3 at, float yaw, int n, float dist, bool shadowFoes) {
    const EnemyKind light[] = {EnemyKind::Hound, EnemyKind::Dazzled, EnemyKind::Brute};
    const EnemyKind dark[] = {EnemyKind::Hound, EnemyKind::Watcher, EnemyKind::Brute};
    for (int i = 0; i < n; ++i) {
        float a = yaw + (float(i) - float(n - 1) * 0.5f) * 0.7f;
        g.devSpawn((shadowFoes ? dark : light)[i % 3], at + Vec3{std::sin(a), 0, std::cos(a)} * dist);
    }
}

// De los ángulos de cámara propuestos (alrededor de `yaw`), el que deja ver más despejado
// al protagonista en `at`.
float clearestYaw(const Game& g, Vec3 at, float yaw, std::initializer_list<float> offsets) {
    float best = yaw, bestClear = -1;
    for (float o : offsets) {
        float y = yaw + o;
        Vec3 target = at + Vec3{0, 1.1f, 0};
        Vec3 dir{std::cos(0.32f) * std::sin(y), -std::sin(0.32f), std::cos(0.32f) * std::cos(y)};
        float clear = g.cameraClear(target, target - dir * 6.0f);
        if (clear > bestClear + 0.05f) bestClear = clear, best = y;
    }
    return best;
}

bool nearPortal(const Game& g, Vec3 p, float r) {
    for (const Portal& o : g.portals())
        if (length(flat(o.pos - p)) < r) return true;
    return false;
}

}  // namespace

bool Showcase::done() const { return act_ >= kActCount; }
const char* Showcase::title() const { return act_ >= 0 && act_ < kActCount ? kActs[act_].title : ""; }
const char* Showcase::subtitle() const { return act_ >= 0 && act_ < kActCount ? kActs[act_].subtitle : ""; }
float Showcase::captionAlpha() const {
    if (act_ < 0 || act_ >= kActCount) return 0;
    float s = float(t_) / 60.0f;
    return std::min(1.0f, s * 4.0f) * clampf((3.5f - s) / 0.6f, 0, 1);
}

Controls Showcase::step(Game& g, float& camYaw, const Controls& base) {
    Controls c;
    c.camForward = base.camForward;
    c.aimOrigin = base.aimOrigin;
    c.aimDir = base.aimDir;
    if (act_ < 0 || ++t_ >= kActs[act_].steps) {  // siguiente acto
        ++act_;
        t_ = 0;
        if (done()) return c;
        g.setTestCheats(true, true);
        g.devNoSpawns(true);
        g.devClearEnemies();
        const auto& lamps = g.lamps();
        auto place = [&](Vec3 p, float yaw) {
            g.devPlace(p, yaw);
            camYaw = yaw;
            cut_ = true;
        };
        auto openSpot = [&]() {  // lejos de las farolas, para pelear con sitio
            Vec3 p{0, 0, 0};
            for (float x = 0; x < 16; x += 2.0f) {
                p = {x, 0, -x * 0.5f};
                if (g.devFreeSpot(p, 3.0f) && !nearPortal(g, p, 5.0f)) break;
            }
            return p;
        };
        switch (act_) {
            case 0: case 1: case 2: case 3: case 8: case 9: case 10: case 11: {
                bool shadowAct = act_ >= 8;
                if (g.shadowMode() != shadowAct) c.swapMode = true;
                home_ = openSpot();
                place(home_, 0);
                int n = act_ == 2 || act_ == 10 ? 6 : act_ == 0 || act_ == 8 ? 3 : 4;
                spawnRing(g, home_, 0, n, act_ == 2 || act_ == 10 ? 4.0f : 5.5f, shadowAct);
                break;
            }
            case 4: {  // dos farolas a la vista una de otra
                lampA_ = lampB_ = -1;
                float best = 1e9f;
                for (size_t i = 0; i < lamps.size(); ++i)
                    for (size_t k = 0; k < lamps.size(); ++k) {
                        if (i == k || !lamps[i].on || !lamps[k].on || !lamps[i].bulb || !lamps[k].bulb) continue;
                        float d = length(flat(lamps[i].pos - lamps[k].pos));
                        if (d > 8.0f && d < 22.0f && std::fabs(d - 14.0f) < best) best = std::fabs(d - 14.0f), lampA_ = int(i), lampB_ = int(k);
                    }
                if (lampA_ < 0) break;
                Vec3 a = lamps[size_t(lampA_)].pos, b = lamps[size_t(lampB_)].pos;
                Vec3 p = flat(a) + normalize(flat(a - b)) * 3.5f;
                g.devFreeSpot(p, 0.6f);
                place(p, yawTo(p, a));
                break;
            }
            case 5: {  // una farola encendida que no sea la del viaje
                int pick = -1;
                for (size_t i = 0; i < lamps.size(); ++i)
                    if (lamps[i].bulb && lamps[i].on && !lamps[i].broken && int(i) != lampA_ && int(i) != lampB_) pick = int(i);
                if (pick < 0) break;
                Vec3 l = flat(lamps[size_t(pick)].pos);
                Vec3 p = l + Vec3{0, 0, -1.4f};
                place(p, yawTo(p, l));
                camYaw = clearestYaw(g, p, camYaw, {1.3f, -1.3f, 0.9f, -0.9f});  // de lado: farola y golpe
                home_ = l;
                break;
            }
            case 6: {  // un interruptor con sus farolas aún encendidas
                switch_ = -1;
                const auto& sw = g.lampSwitches();
                for (size_t i = 0; i < sw.size() && switch_ < 0; ++i) {
                    bool alive = false;
                    for (int li : sw[i].lamps) alive |= !lamps[size_t(li)].broken;
                    if (alive && sw[i].on) switch_ = int(i);
                }
                if (switch_ < 0) break;
                const LampSwitch& s = sw[size_t(switch_)];
                Vec3 mid;
                for (int li : s.lamps) mid += flat(lamps[size_t(li)].pos);
                mid = mid * (1.0f / float(s.lamps.size()));
                // Junto al interruptor, de cara a él; la cámara, de lado y mirando hacia sus farolas.
                Vec3 toMid = normalize(flat(mid - s.pos)), side{-toMid.z, 0, toMid.x};
                Vec3 p = flat(s.pos) + side * 0.9f;
                place(p, yawTo(p, s.pos));
                camYaw = clearestYaw(g, p, yawTo(s.pos, mid), {0.5f, -0.5f, 0.9f, -0.9f, 0.0f});
                break;
            }
            case 7:
                home_ = openSpot();
                place(home_, 0);
                break;
            case 12: {  // el rincón más oscuro con sitio para moverse
                if (!g.shadowMode()) c.swapMode = true;
                Vec3 best{0, 0, 0};
                float bestDark = -1;
                for (float x = -18; x <= 18; x += 1.5f)
                    for (float z = -18; z <= 18; z += 1.5f) {
                        Vec3 q{x, 0, z}, f = q;
                        if (!g.devFreeSpot(f, 1.0f) || length(f - q) > 0.1f || nearPortal(g, q, 6.0f)) continue;
                        float d = 0;
                        for (int k = 0; k < 6; ++k) {
                            float a = float(k) * kPi / 3.0f;
                            d += g.shadowAt(q + Vec3{std::cos(a), 0, std::sin(a)} * 2.5f);
                        }
                        d += 3.0f * g.shadowAt(q);
                        if (d > bestDark) bestDark = d, best = q;
                    }
                home_ = best;
                // De cara a la luz más cercana, para que se vea dónde termina la sombra.
                float yaw = 0, bd = 1e9f;
                for (const Lamp& l : lamps)
                    if (l.on && !l.broken && length(flat(l.pos - best)) < bd) bd = length(flat(l.pos - best)), yaw = yawTo(best, l.pos);
                place(best, yaw);
                camYaw = clearestYaw(g, best, yaw, {0.0f, 0.6f, -0.6f, kPi});
                break;
            }
            case 14: case 15: {
                bool shadowAct = act_ == 15;
                if (g.shadowMode() != shadowAct) c.swapMode = true;
                home_ = openSpot();
                place(home_, 0);
                g.devSpawn(EnemyKind::Dazzled, home_ + Vec3{0, 0, 2.2f});
                g.devToughen(0, 8.0f);
                break;
            }
            case 13: {  // la cara oscura de un bloque alto, con sitio delante
                if (!g.shadowMode()) c.swapMode = true;
                const Vec3 normals[4] = {{-1, 0, 0}, {1, 0, 0}, {0, 0, -1}, {0, 0, 1}};
                float best = -1;
                Vec3 at, face;
                for (const AABB& b : g.world().solids) {
                    float h = b.max.y - b.min.y;
                    if (b.min.y > 0.1f || h < 2.5f || h > 7.0f) continue;
                    Vec3 mid = (b.min + b.max) * 0.5f, half = (b.max - b.min) * 0.5f;
                    for (const Vec3& n : normals) {
                        float w = n.x != 0 ? half.z : half.x;
                        if (w < 1.0f) continue;
                        Vec3 f{mid.x + n.x * half.x, 0, mid.z + n.z * half.z};
                        Vec3 p = f + n * 3.0f, q = p;
                        if (!g.devFreeSpot(q, 0.6f) || length(q - p) > 0.1f || nearPortal(g, p, 5.0f)) continue;
                        float dark = g.shadowAt(f + n * 0.2f + Vec3{0, 1, 0}) + g.shadowAt(p);
                        if (dark > best) best = dark, at = p, face = f;
                    }
                }
                home_ = face;
                dir_ = normalize(flat(face - at));
                place(at, yawTo(at, face));
                camYaw = clearestYaw(g, at, camYaw, {0.5f, -0.5f, 0.0f, 0.9f, -0.9f});
                break;
            }
        }
        return c;
    }

    int t = t_;
    switch (act_) {
        case 0: case 8:
            fight(g, c, t);
            break;
        case 1: case 9: {
            const int at[] = {30, 150, 270, 390};
            const int cmd1[] = {0, 1, 3, -1}, cmd9[] = {0, 1, 2, 3};
            Vec3 foe;
            if (!g.lockTarget(foe)) c.lockOn = t % 15 == 0;
            for (int k = 0; k < 4; ++k)
                if (t == at[k]) c.command = act_ == 1 ? cmd1[k] : cmd9[k];
            if (t > 440) fight(g, c, t);
            break;
        }
        case 2: case 10:
            if (t == 40) c.command = 4;
            break;
        case 3: case 11:
            if (t == 20) c.transform = true;
            if (t > 70 && t < kActs[act_].steps - 60) fight(g, c, t, 9);
            if (t == kActs[act_].steps - 40) c.transform = true;
            break;
        case 4:
            if (t == 40) c.light = true;           // entrar en la farola
            if (t == 120 || t == 330) c.lockOn = true;  // elegir la siguiente fuente
            if (t == 180 || t == 390) c.light = true;   // saltar
            if (t == 560) c.jump = true;           // salir
            break;
        case 5:
            if (t > 40 && t % 24 == 0) c.attack = true;
            break;
        case 6:
            if (t == 70 || t == 250 || t == 430) c.interact = true;
            break;
        case 7:
            if (t == 50) c.swapMode = true;
            break;
        case 12: {
            if (t == 40) c.light = true;  // fundirse
            if (t > 80 && t < 400) {      // se desliza en círculo (la luz le frena)
                float a = float(t - 80) / 320.0f * 2.0f * kPi;
                c.move = Vec3{std::cos(a), 0, std::sin(a)};
            }
            if (t == 400) spawnRing(g, g.hero().body.pos, g.hero().yaw, 2, 2.0f, true);
            if (t == 440) c.attack = true;  // emboscada
            if (t > 480) fight(g, c, t);
            break;
        }
        case 14: case 15: {
            // Guardia justo cuando el Gris termina de prepararse; con el contraataque abierto, ataca.
            Vec3 foe;
            if (!g.lockTarget(foe)) c.lockOn = t % 15 == 0;
            // Cada 4 s: levanta la guardia y el Gris ataca justo entonces; con el contraataque
            // abierto, ataca.
            int k = t % 240;
            if (k == 40 && !g.riposting()) guard_ = 30;
            if (k == 43 && !g.riposting()) g.devAttackNow(0);
            c.guard = guard_ > 0 && !g.riposting();
            guard_ = std::max(0, guard_ - 1);
            if (g.riposteWindow() > 0 && !g.riposting() && k > 75) c.attack = true;
            break;
        }
        case 13:
            if (t == 30) c.light = true;
            if (t > 60 && t < 330) c.move = dir_;  // hacia la pared, y sigue empujando: sube
            if (t == 400) c.light = true;  // sale, ya en el tejado
            break;
    }
    return c;
}
