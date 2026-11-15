#include "render/hero_model.h"

#include "render/debug_draw.h"
#include "render/glb.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace {

HeroGlbModel gVela, gHollin;

// Grupos del .glb → huesos del juego.
struct GroupJoint {
    const char* name;
    int joint;
};
const GroupJoint kBoneGroups[] = {
    {"Hips", Pelvis},          {"Spine", Spine},          {"Neck", Neck},           {"Head", Head},
    {"Shoulder_L", ShoulderL}, {"UpperArm_L", ShoulderL}, {"LowerArm_L", ElbowL},   {"Hand_L", WristL},
    {"Shoulder_R", ShoulderR}, {"UpperArm_R", ShoulderR}, {"LowerArm_R", ElbowR},   {"Hand_R", WristR},
    {"UpperLeg_L", HipL},      {"LowerLeg_L", KneeL},     {"Foot_L", AnkleL},       {"UpperLeg_R", HipR},
    {"LowerLeg_R", KneeR},     {"Foot_R", AnkleR},        {"Scarf", Chest},
};
// Piezas con inercia: grupo, tipo y hueso del que cuelgan.
struct GroupPart {
    const char* name;
    HeroPartKind kind;
    int joint;
};
const GroupPart kPartGroups[] = {
    {"Ponytail", HeroPartKind::Hair, Head},           {"Mane", HeroPartKind::Hair, Head},
    {"Ahoge", HeroPartKind::Ahoge, Head},             {"Scarf_Tail_Front", HeroPartKind::ScarfFront, Chest},
    {"Scarf_Tail_Back", HeroPartKind::ScarfBack, Chest}, {"Lantern", HeroPartKind::Lantern, Pelvis},
};

uint8_t toSrgb(float lin) {
    lin = std::clamp(lin, 0.0f, 1.0f);
    float s = lin <= 0.0031308f ? lin * 12.92f : 1.055f * std::pow(lin, 1.0f / 2.4f) - 0.055f;
    return uint8_t(std::lround(s * 255.0f));
}

bool startsWith(const std::string& s, const char* p) { return s.compare(0, std::strlen(p), p) == 0; }

// Color y material del juego para un material del .glb.
void materialOf(const GlbMaterial& m, uint32_t& color, float& mat, bool& glow) {
    glow = m.emissive[0] + m.emissive[1] + m.emissive[2] > 0.05f;
    const float* c = m.base;
    color = rgba(toSrgb(c[0]), toSrgb(c[1]), toSrgb(c[2]));
    mat = float(int(Material::Cloth));
    if (glow) {
        mat = float(int(Material::Glow));
    } else if (startsWith(m.name, "Hair")) {
        mat = 7.0f;  // pelo: brillo en banda
    } else if (m.name == "Iron" || startsWith(m.name, "Gold")) {
        mat = float(int(Material::Metal));
    }
}

// Lo que sube a la GPU: una malla con skinning.
struct MeshBuild {
    std::vector<SkinVertex> verts;
    std::vector<uint32_t> indices;
    Vec3 glowSum;
    int glowCount = 0;
};

// Pesos del torso según la altura (y el lado, en el faldón): pelvis, espalda, pecho y, por
// debajo de la cadera, los muslos, para que el abrigo se abra al andar.
void torsoWeights(Vec3 p, const Mat4* bind, int joints[4], float w[4]) {
    float pelvisY = translationOf(bind[Pelvis]).y, spineY = translationOf(bind[Spine]).y, chestY = translationOf(bind[Chest]).y;
    float hipY = translationOf(bind[HipL]).y;
    for (int i = 0; i < 4; ++i) joints[i] = Pelvis, w[i] = 0;
    if (p.y >= chestY) {
        joints[0] = Chest, w[0] = 1;
    } else if (p.y >= spineY) {
        float t = smoothstep01((p.y - spineY) / (chestY - spineY));
        joints[0] = Spine, w[0] = 1 - t, joints[1] = Chest, w[1] = t;
    } else if (p.y >= pelvisY) {
        float t = smoothstep01((p.y - pelvisY) / (spineY - pelvisY));
        joints[0] = Pelvis, w[0] = 1 - t, joints[1] = Spine, w[1] = t;
    } else {
        float leg = 0.75f * smoothstep01((hipY - 0.02f - p.y) / 0.35f);
        float s = smoothstep01((p.x + 0.05f) / 0.1f);  // 1 = lado izquierdo (+X)
        joints[0] = Pelvis, w[0] = 1 - leg;
        joints[1] = HipL, w[1] = leg * s;
        joints[2] = HipR, w[2] = leg * (1 - s);
    }
}

void addVertex(MeshBuild& b, Vec3 p, Vec3 n, uint32_t color, float mat, const int joints[4], const float w[4]) {
    SkinVertex v{};
    v.pos[0] = p.x, v.pos[1] = p.y, v.pos[2] = p.z;
    v.normal[0] = n.x, v.normal[1] = n.y, v.normal[2] = n.z;
    v.color = color;
    v.material = mat;
    int sum = 0, heaviest = 0;
    for (int i = 0; i < 4; ++i) {
        v.joints[i] = uint8_t(joints[i]);
        v.weights[i] = uint8_t(std::lround(std::clamp(w[i], 0.0f, 1.0f) * 255.0f));
        sum += v.weights[i];
        if (v.weights[i] > v.weights[heaviest]) heaviest = i;
    }
    v.weights[heaviest] = uint8_t(std::clamp(int(v.weights[heaviest]) + 255 - sum, 0, 255));  // que sumen 255
    b.verts.push_back(v);
}

// Añade las mallas de un nodo. `toSpace`: de mundo al espacio en que se guarda la malla.
// `weights`: pesos de cada vértice (en mundo).
template <class Weights>
void addNodeMesh(const GlbFile& g, const GlbNode& n, const Mat4& toSpace, MeshBuild& b, Weights weights, bool* anyGlow = nullptr) {
    Mat4 m = toSpace * n.world;
    Mat4 nm = affineInverse(m);  // normales: (M⁻¹)ᵀ
    for (const GlbPrimitive& prim : g.meshes[size_t(n.mesh)]) {
        GlbMaterial mat;
        if (prim.material >= 0 && size_t(prim.material) < g.materials.size()) mat = g.materials[size_t(prim.material)];
        uint32_t color;
        float matId;
        bool glow;
        materialOf(mat, color, matId, glow);
        if (anyGlow) *anyGlow |= glow;
        auto emit = [&](bool flip) {
            uint32_t base = uint32_t(b.verts.size());
            for (size_t i = 0; i < prim.positions.size(); ++i) {
                Vec3 world = transformPoint(n.world, prim.positions[i]);
                Vec3 p = transformPoint(m, prim.positions[i]);
                Vec3 nr = i < prim.normals.size() ? prim.normals[i] : Vec3{0, 1, 0};
                Vec3 q{nm.m[0] * nr.x + nm.m[1] * nr.y + nm.m[2] * nr.z, nm.m[4] * nr.x + nm.m[5] * nr.y + nm.m[6] * nr.z,
                       nm.m[8] * nr.x + nm.m[9] * nr.y + nm.m[10] * nr.z};
                q = normalize(q);
                if (flip) q = -q;
                int joints[4];
                float w[4];
                weights(world, joints, w);
                addVertex(b, p, q, color, matId, joints, w);
                if (glow) b.glowSum += p, ++b.glowCount;
            }
            for (size_t i = 0; i + 2 < prim.indices.size(); i += 3) {
                uint32_t a = prim.indices[i], c = prim.indices[i + 1], d = prim.indices[i + 2];
                if (flip) std::swap(c, d);
                b.indices.insert(b.indices.end(), {base + a, base + c, base + d});
            }
        };
        emit(false);
        if (mat.doubleSided) emit(true);  // telas finas: también por detrás
    }
}

// El primer antepasado (o el propio nodo) cuyo nombre esté en la tabla.
template <class Table, size_t N>
const Table* groupOf(const GlbFile& g, int node, const Table (&table)[N], int* at = nullptr) {
    for (int i = node; i >= 0; i = g.nodes[size_t(i)].parent)
        for (const Table& t : table)
            if (g.nodes[size_t(i)].name == t.name) {
                if (at) *at = i;
                return &t;
            }
    return nullptr;
}

bool isUnder(const GlbFile& g, int node, const char* name) {
    for (int i = node; i >= 0; i = g.nodes[size_t(i)].parent)
        if (g.nodes[size_t(i)].name == name) return true;
    return false;
}

}  // namespace

bool loadHeroGlb(Renderer& r, const std::string& path, HeroGlbModel& out, std::string& error) {
    GlbFile g;
    if (!g.load(path, error)) return false;
    // Postura de reposo de cada hueso: la del grupo del .glb (el pecho, que el .glb no tiene,
    // a media espalda; las puntas de los pies, delante del tobillo).
    Mat4 bind[kMaxJoints];
    for (const GroupJoint& gj : kBoneGroups) {
        if (!std::strncmp(gj.name, "Shoulder_", 9) || !std::strcmp(gj.name, "Scarf")) continue;  // el brazo manda
        int i = g.find(gj.name);
        if (i < 0) return error = path + ": falta el grupo " + gj.name, false;
        bind[gj.joint] = g.nodes[size_t(i)].world;
    }
    float spineY = translationOf(bind[Spine]).y, neckY = translationOf(bind[Neck]).y;
    bind[Chest] = bind[Spine] * translate({0, (neckY - spineY) * 0.47f, 0});
    bind[ToeL] = bind[AnkleL] * translate({0, -0.06f, 0.12f});
    bind[ToeR] = bind[AnkleR] * translate({0, -0.06f, 0.12f});
    Mat4 invBind[kMaxJoints];
    for (int j = 0; j < JointCount; ++j) invBind[j] = affineInverse(bind[j]);

    MeshBuild body, detail, prop;
    struct PartBuild {
        HeroPart part;
        MeshBuild mesh;
    };
    std::vector<PartBuild> parts;
    int propNode = std::max(g.find("Prop_Sword"), g.find("Prop_Hammer"));
    Mat4 propInv = propNode >= 0 ? affineInverse(g.nodes[size_t(propNode)].world) : Mat4::identity();
    float propTop = 0;

    for (size_t ni = 0; ni < g.nodes.size(); ++ni) {
        const GlbNode& n = g.nodes[ni];
        if (n.mesh < 0 || size_t(n.mesh) >= g.meshes.size()) continue;
        int node = int(ni);
        if (propNode >= 0 && isUnder(g, node, g.nodes[size_t(propNode)].name.c_str())) {  // el arma
            auto one = [](Vec3, int j[4], float w[4]) {
                for (int i = 0; i < 4; ++i) j[i] = 0, w[i] = i == 0 ? 1.0f : 0.0f;
            };
            addNodeMesh(g, n, propInv, prop, one);
            continue;
        }
        int partNode = -1;
        if (const GroupPart* gp = groupOf(g, node, kPartGroups, &partNode)) {  // pieza con inercia
            auto it = std::find_if(parts.begin(), parts.end(), [&](const PartBuild& p) { return p.part.kind == gp->kind; });
            if (it == parts.end()) {
                PartBuild pb;
                pb.part.kind = gp->kind;
                pb.part.joint = gp->joint;
                pb.part.inverseBind = invBind[gp->joint];
                pb.part.pivot = transformPoint(invBind[gp->joint], translationOf(g.nodes[size_t(partNode)].world));
                parts.push_back(std::move(pb));
                it = parts.end() - 1;
            }
            int joint = gp->joint;
            auto one = [joint](Vec3, int j[4], float w[4]) {
                for (int i = 0; i < 4; ++i) j[i] = joint, w[i] = i == 0 ? 1.0f : 0.0f;
            };
            addNodeMesh(g, n, Mat4::identity(), it->mesh, one);
            continue;
        }
        const GroupJoint* gj = groupOf(g, node, kBoneGroups);
        if (!gj) continue;  // fuera del esqueleto
        bool torso = gj->joint == Pelvis || gj->joint == Spine;
        int joint = gj->joint;
        auto weights = [&](Vec3 p, int j[4], float w[4]) {
            if (torso) return torsoWeights(p, bind, j, w);
            for (int i = 0; i < 4; ++i) j[i] = joint, w[i] = i == 0 ? 1.0f : 0.0f;
        };
        // La cara (calcas) y lo que brilla, sin contorno.
        bool glow = false;
        if (n.mesh >= 0)
            for (const GlbPrimitive& p : g.meshes[size_t(n.mesh)])
                if (p.material >= 0 && size_t(p.material) < g.materials.size()) {
                    const GlbMaterial& m = g.materials[size_t(p.material)];
                    glow |= m.emissive[0] + m.emissive[1] + m.emissive[2] > 0.05f;
                }
        bool face = isUnder(g, node, "Face");
        addNodeMesh(g, n, Mat4::identity(), face || glow ? detail : body, weights);
    }
    if (body.verts.empty()) return error = path + ": no hay mallas en el esqueleto", false;

    auto upload = [&](MeshBuild& b, BuiltModel& m) {
        m.mesh = b.verts.empty() ? -1 : r.createSkinnedMesh(b.verts, b.indices);
        m.jointCount = JointCount;
        m.triangles = b.indices.size() / 3;
        for (int j = 0; j < JointCount; ++j) m.inverseBind[j] = invBind[j];
    };
    out = HeroGlbModel{};
    upload(body, out.body);
    upload(detail, out.detail);
    for (PartBuild& pb : parts) {
        // Las piezas se guardan en mundo con su hueso como único hueso (índice del juego): al
        // dibujarlas se pasan matrices hasta ese índice.
        pb.part.mesh = r.createSkinnedMesh(pb.mesh.verts, pb.mesh.indices);
        if (pb.mesh.glowCount > 0) {
            pb.part.hasGlow = true;
            pb.part.glow = transformPoint(invBind[pb.part.joint], pb.mesh.glowSum * (1.0f / float(pb.mesh.glowCount)));
        }
        out.parts.push_back(pb.part);
    }
    if (!prop.verts.empty()) {
        out.prop = r.createSkinnedMesh(prop.verts, prop.indices);
        for (const SkinVertex& v : prop.verts) propTop = std::max(propTop, v.pos[1]);
        out.propLength = propTop;
    }
    // Color de la luz del farol: el del material «..._Emissive» (o el primero que brille).
    for (int pass = 0; pass < 2 && !out.glowColor; ++pass)
        for (const GlbMaterial& m : g.materials)
            if (m.emissive[0] + m.emissive[1] + m.emissive[2] > 0.05f &&
                (pass == 1 || m.name.find("_Emissive") != std::string::npos)) {
                out.glowColor = rgba(toSrgb(m.emissive[0]), toSrgb(m.emissive[1]), toSrgb(m.emissive[2]));
                break;
            }
    out.loaded = true;
    std::printf("%s: %zu + %zu triangulos, %zu piezas con inercia, arma de %.2f m\n", path.c_str(), out.body.triangles,
                out.detail.triangles, out.parts.size(), out.propLength);
    return true;
}

void loadHeroModels(Renderer& r, const std::string& assetDir) {
    std::string error;
    if (!loadHeroGlb(r, assetDir + "/models/vela.glb", gVela, error)) std::fprintf(stderr, "Vela: %s\n", error.c_str());
    if (!loadHeroGlb(r, assetDir + "/models/hollin.glb", gHollin, error)) std::fprintf(stderr, "Hollín: %s\n", error.c_str());
}

const HeroGlbModel* heroGlb(HeroLook look) {
    const HeroGlbModel& m = look == HeroLook::Shadow ? gHollin : gVela;
    return m.loaded ? &m : nullptr;
}
