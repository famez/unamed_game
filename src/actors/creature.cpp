#include "actors/creature.h"

#include "render/glb.h"

#include <cmath>
#include <cstdio>
#include <vector>

namespace {

struct CreatureInfo {
    const char* file;
    const char* name;
    float scale;
};
const CreatureInfo kInfo[int(CreatureKind::Count)] = {
    {"mechita", "Mechita", 1.3f},
    {"hollinete", "Hollinete", 1.0f},
    {"buho", "Búho de cobre", 1.0f},
    {"topo", "Topo minero", 1.0f},
    {"caracol", "Caracol mercader", 1.0f},
    {"larguero", "Larguero", 1.0f},
    {"ballena", "Ballena de niebla", 1.0f},
};

CreatureRig gRigs[int(CreatureKind::Count)];

// Rotación local de un hueso por su nombre (si la criatura no lo tiene, va a parar a un sumidero).
struct Bones {
    const CreatureRig& rig;
    Pose& pose;
    Vec3 sink;
    Vec3& at(const char* name) {
        int i = rig.find(name);
        if (i < 0) return sink = {}, sink;
        return pose.rotation[i];
    }
};

}  // namespace

const char* creatureFile(CreatureKind k) { return kInfo[int(k)].file; }
const char* creatureName(CreatureKind k) { return kInfo[int(k)].name; }
float creatureScale(CreatureKind k) { return kInfo[int(k)].scale; }

int CreatureRig::find(const char* name) const {
    for (int i = 0; i < count; ++i)
        if (names[i] == name) return i;
    return -1;
}

bool creatureRigFromGlb(const GlbFile& g, CreatureRig& out, std::string& error) {
    if (g.skins.empty()) return error = "no tiene esqueleto", false;
    const GlbSkin& skin = g.skins[0];
    int n = int(skin.joints.size());
    if (n == 0 || n > kMaxJoints) return error = "tiene " + std::to_string(n) + " huesos (como mucho " + std::to_string(kMaxJoints) + ")", false;
    // Padre de cada hueso dentro de la piel: el primer antepasado que también es hueso.
    std::vector<int> parent(size_t(n), -1);
    for (int i = 0; i < n; ++i)
        for (int a = g.nodes[size_t(skin.joints[size_t(i)])].parent; a >= 0 && parent[size_t(i)] < 0; a = g.nodes[size_t(a)].parent)
            for (int k = 0; k < n; ++k)
                if (skin.joints[size_t(k)] == a) parent[size_t(i)] = k;
    // Padres antes que hijos (lo pide computeWorldMatrices).
    std::vector<int> order, placed(size_t(n), -1);
    while (int(order.size()) < n) {
        size_t before = order.size();
        for (int i = 0; i < n; ++i)
            if (placed[size_t(i)] < 0 && (parent[size_t(i)] < 0 || placed[size_t(parent[size_t(i)])] >= 0)) {
                placed[size_t(i)] = int(order.size());
                order.push_back(i);
            }
        if (order.size() == before) return error = "la jerarquía de huesos tiene un ciclo", false;
    }
    out.count = n;
    for (int o = 0; o < n; ++o) {
        int i = order[size_t(o)], p = parent[size_t(i)];
        Mat4 world = affineInverse(skin.inverseBind[size_t(i)]);
        Mat4 local = p >= 0 ? skin.inverseBind[size_t(p)] * world : world;  // la inversa de bind del padre lo lleva a su espacio
        out.names[o] = g.nodes[size_t(skin.joints[size_t(i)])].name;
        out.joints[o] = {out.names[o].c_str(), p >= 0 ? placed[size_t(p)] : -1, translationOf(local), {}, {}};
    }
    return true;
}

int loadCreatureRigs(const std::string& dir) {
    int loaded = 0;
    for (int k = 0; k < int(CreatureKind::Count); ++k) {
        CreatureRig& rig = gRigs[k];
        rig.count = 0;
        std::string path = dir + "/" + kInfo[k].file + ".glb", error;
        FILE* f = std::fopen(path.c_str(), "rb");
        if (!f) continue;  // no está: esa criatura no sale
        std::fclose(f);
        GlbFile g;
        if (!g.load(path, error) || !creatureRigFromGlb(g, rig, error)) {
            rig.count = 0;
            std::fprintf(stderr, "%s: %s\n", path.c_str(), error.c_str());
            continue;
        }
        ++loaded;
    }
    return loaded;
}

const CreatureRig& creatureRig(CreatureKind k) { return gRigs[int(k)]; }

void poseCreature(CreatureKind kind, const CreatureRig& rig, const CreatureMotion& m, Pose& out) {
    out = Pose{};
    Bones b{rig, out, {}};
    Vec3& root = out.rootOffset;
    float t = m.time;
    float s = std::sin(m.phase), moving = m.speed > 0.05f ? 1.0f : 0.0f;
    switch (kind) {
        case CreatureKind::Mechita: {
            // Saltitos al andar; quieta, la llama (la cabeza) tiembla y se balancea.
            root.y = 0.07f * std::fabs(s) * moving;
            b.at("leg_l").x = -0.6f * s * moving;
            b.at("leg_r").x = 0.6f * s * moving;
            b.at("arm_l").z = 0.25f + 0.35f * std::fabs(s) * moving + 0.05f * std::sin(t * 1.7f);
            b.at("arm_r").z = -0.25f - 0.35f * std::fabs(s) * moving - 0.05f * std::sin(t * 1.9f);
            b.at("body").x = 0.15f * moving;
            b.at("body").z = 0.04f * std::sin(t * 1.3f);
            b.at("head").z = 0.07f * std::sin(t * 7.0f) + 0.04f * std::sin(t * 11.3f);
            b.at("head").x = 0.05f * std::sin(t * 5.1f);
            b.at("head").y = m.look * 0.6f;
            break;
        }
        case CreatureKind::Hollinete: {
            if (m.hop >= 0) {  // estirado en el aire
                float u = m.hop;
                b.at("fl").x = b.at("fr").x = -0.9f;
                b.at("hl").x = b.at("hr").x = 0.9f;
                b.at("hips").x = lerpf(0.25f, -0.25f, u);
                b.at("tail1").x = -0.6f;
            } else if (moving > 0) {  // pares diagonales
                b.at("fl").x = b.at("hr").x = -0.55f * s;
                b.at("fr").x = b.at("hl").x = 0.55f * s;
                root.y = 0.012f * std::fabs(std::cos(m.phase));
            } else {  // sentado: los cuartos traseros abajo y las patas de delante rectas
                b.at("hips").x = -0.45f;
                b.at("fl").x = b.at("fr").x = 0.45f;
                b.at("hl").x = b.at("hr").x = -0.6f;
                root.y = -0.05f;
                b.at("chest").x = 0.012f * std::sin(t * 1.4f);  // respira
            }
            b.at("tail1").x += -0.3f + 0.15f * std::sin(t * 1.1f);
            b.at("tail1").y = 0.45f * std::sin(t * 0.7f);
            b.at("tail2").y = 0.55f * std::sin(t * 0.7f - 0.9f);
            b.at("neck").y = m.look * 0.45f;
            b.at("head").y = m.look * 0.55f;
            b.at("head").z = 0.12f * std::sin(t * 0.5f);
            break;
        }
        case CreatureKind::Buho: {
            bool flying = m.hop >= 0;
            float flap = std::sin(m.phase);
            b.at("wing_l").z = flying ? 1.1f + 0.7f * flap : 0.04f * std::sin(t * 0.9f);
            b.at("wing_r").z = flying ? -1.1f - 0.7f * flap : -0.04f * std::sin(t * 0.9f);
            b.at("body").x = flying ? 0.5f : 0.02f * std::sin(t * 1.2f);
            b.at("tail").x = flying ? -0.4f : 0.0f;
            b.at("head").y = m.look;
            b.at("head").x = flying ? -0.4f : 0.0f;
            break;
        }
        case CreatureKind::Topo: {
            // Cava con las dos manos-pala por turnos, encorvado; atento, se yergue.
            float dig = 1.0f - m.hide;
            float a = std::sin(t * 4.5f), bb = std::sin(t * 4.5f + kPi);
            b.at("spine").x = 0.35f * dig;
            b.at("head").x = 0.2f * dig;
            b.at("arm_l").x = (-0.7f + 0.6f * a) * dig;
            b.at("arm_r").x = (-0.7f + 0.6f * bb) * dig;
            b.at("hand_l").x = 0.5f * std::max(0.0f, a) * dig;
            b.at("hand_r").x = 0.5f * std::max(0.0f, bb) * dig;
            b.at("head").y = m.look;
            b.at("spine").z = 0.02f * std::sin(t * 1.3f);
            // Atento: saluda con la mano derecha de vez en cuando.
            b.at("arm_r").z -= m.hide * (1.4f + 0.25f * std::sin(t * 7.0f)) * (std::fmod(t, 6.0f) < 2.0f ? 1.0f : 0.0f);
            break;
        }
        case CreatureKind::Caracol: {
            // Los ojos en sus pedúnculos van a su aire; escondido, se pliega todo hacia la concha.
            float h = smoothstep01(m.hide);
            b.at("neck").x = 0.12f * std::sin(t * 0.8f) * (1 - h) - 0.8f * h;  // hacia atrás, contra la concha
            b.at("neck").y = m.look * 0.6f * (1 - h);
            b.at("head").y = m.look * 0.4f * (1 - h);
            b.at("stalk_l").z = 0.2f * std::sin(t * 1.3f) - 0.1f;
            b.at("stalk_r").z = -0.2f * std::sin(t * 1.1f + 1.0f) + 0.1f;
            b.at("stalk_l").x = 0.15f * std::sin(t * 0.9f) * (1 - h) - 1.4f * h;
            b.at("stalk_r").x = 0.15f * std::sin(t * 0.7f) * (1 - h) - 1.4f * h;
            b.at("shell").x = 0.03f * std::sin(m.phase) * moving;
            b.at("shell").z = 0.04f * std::sin(m.phase * 0.5f) * moving;
            break;
        }
        case CreatureKind::Larguero: {
            // Como un humanoide, pero a la mitad de velocidad y con zancadas de gigante.
            float k = std::max(0.0f, std::sin(m.phase + 1.2f)), k2 = std::max(0.0f, std::sin(m.phase + 1.2f + kPi));
            b.at("hip_l").x = -0.45f * s * moving;
            b.at("hip_r").x = 0.45f * s * moving;
            b.at("knee_l").x = 0.7f * k * moving;
            b.at("knee_r").x = 0.7f * k2 * moving;
            b.at("shoulder_l").x = 0.3f * s * moving;
            b.at("shoulder_r").x = -0.3f * s * moving;
            b.at("elbow_l").x = b.at("elbow_r").x = -0.15f;
            root.y = -0.04f * std::fabs(std::cos(m.phase)) * moving;
            b.at("chest").x = 0.01f * std::sin(t * 0.8f) + 0.08f;  // un poco cargado de hombros
            b.at("head").y = m.look;
            if (m.lighting >= 0) {  // alza el farol hasta la farola y lo baja
                float u = std::sin(std::min(1.0f, m.lighting / 2.2f) * kPi);
                b.at("shoulder_r").x = -2.1f * u;
                b.at("elbow_r").x = -0.15f - 0.3f * u;
                b.at("head").x = -0.35f * u;
                b.at("chest").x -= 0.1f * u;
            }
            break;
        }
        case CreatureKind::Ballena: {
            // Onda lenta y vertical de la cola, aletas que reman y la boca que se entreabre.
            b.at("tail1").x = 0.14f * std::sin(t * 0.6f);
            b.at("tail2").x = 0.3f * std::sin(t * 0.6f - 0.9f);
            b.at("fin_l").z = 0.25f * std::sin(t * 0.6f + 0.5f);
            b.at("fin_r").z = -0.25f * std::sin(t * 0.6f + 0.5f);
            b.at("fin_l").y = b.at("fin_r").y = 0.0f;
            b.at("jaw").x = 0.18f * std::max(0.0f, std::sin(t * 0.13f));
            b.at("body").x = 0.04f * std::sin(t * 0.25f);
            b.at("body").z = -0.08f;  // se inclina hacia dentro de la vuelta
            break;
        }
        case CreatureKind::Count: break;
    }
}
