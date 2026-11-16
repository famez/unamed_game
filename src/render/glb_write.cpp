#include "render/glb_write.h"

#include "render/glb.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace {

float srgbToLinear(uint8_t c) {
    float s = float(c) / 255.0f;
    return s <= 0.04045f ? s / 12.92f : std::pow((s + 0.055f) / 1.055f, 2.4f);
}

// 9 cifras: el float vuelve exacto (glTF exige que min/max coincidan con los datos).
std::string num(float v) {
    char b[32];
    std::snprintf(b, sizeof(b), "%.9g", double(v));
    return b;
}

std::string escape(const std::string& s) {
    std::string o;
    for (char c : s) {
        if (c == '"' || c == '\\') o += '\\';
        o += c;
    }
    return o;
}

// Binario del .glb y la lista de bufferViews/accessors que lo describen.
struct Builder {
    std::vector<unsigned char> bin;
    std::string views, accessors;
    int viewCount = 0, accessorCount = 0;

    int view(const void* data, size_t bytes, int target) {
        while (bin.size() % 4) bin.push_back(0);
        size_t at = bin.size();
        const unsigned char* p = static_cast<const unsigned char*>(data);
        bin.insert(bin.end(), p, p + bytes);
        if (viewCount) views += ",";
        views += "{\"buffer\":0,\"byteOffset\":" + std::to_string(at) + ",\"byteLength\":" + std::to_string(bytes);
        if (target) views += ",\"target\":" + std::to_string(target);
        views += "}";
        return viewCount++;
    }
    int accessor(int v, int componentType, size_t count, const char* type, bool normalized = false, const std::string& extra = "") {
        if (accessorCount) accessors += ",";
        accessors += "{\"bufferView\":" + std::to_string(v) + ",\"componentType\":" + std::to_string(componentType) +
                     ",\"count\":" + std::to_string(count) + ",\"type\":\"" + type + "\"";
        if (normalized) accessors += ",\"normalized\":true";
        accessors += extra + "}";
        return accessorCount++;
    }
};

}  // namespace

bool writeSkinnedGlb(const std::string& path, const std::string& name, const std::vector<GlbOutJoint>& joints,
                     const std::vector<GlbOutPrimitive>& prims, const std::vector<GlbOutMaterial>& materials) {
    Builder b;
    std::string primsJson;
    for (const GlbOutPrimitive& p : prims) {
        if (p.verts.empty() || p.indices.empty()) continue;
        size_t n = p.verts.size();
        std::vector<float> pos(n * 3), nor(n * 3), col(n * 4);
        std::vector<uint8_t> jnt(n * 4), wgt(n * 4);
        Vec3 lo{1e9f, 1e9f, 1e9f}, hi{-1e9f, -1e9f, -1e9f};
        for (size_t i = 0; i < n; ++i) {
            const SkinVertex& v = p.verts[i];
            for (int k = 0; k < 3; ++k) pos[i * 3 + size_t(k)] = v.pos[k], nor[i * 3 + size_t(k)] = v.normal[k];
            lo = {std::min(lo.x, v.pos[0]), std::min(lo.y, v.pos[1]), std::min(lo.z, v.pos[2])};
            hi = {std::max(hi.x, v.pos[0]), std::max(hi.y, v.pos[1]), std::max(hi.z, v.pos[2])};
            for (int k = 0; k < 3; ++k) col[i * 4 + size_t(k)] = srgbToLinear(uint8_t((v.color >> (8 * k)) & 0xFF));
            col[i * 4 + 3] = 1.0f;
            // Los huesos sin peso apuntan al 0 (glTF pide índices válidos).
            for (int k = 0; k < 4; ++k) {
                jnt[i * 4 + size_t(k)] = v.weights[k] ? v.joints[k] : 0;
                wgt[i * 4 + size_t(k)] = v.weights[k];
            }
        }
        std::string mm = ",\"min\":[" + num(lo.x) + "," + num(lo.y) + "," + num(lo.z) + "],\"max\":[" + num(hi.x) + "," +
                         num(hi.y) + "," + num(hi.z) + "]";
        int aPos = b.accessor(b.view(pos.data(), pos.size() * 4, 34962), 5126, n, "VEC3", false, mm);
        int aNor = b.accessor(b.view(nor.data(), nor.size() * 4, 34962), 5126, n, "VEC3");
        int aCol = b.accessor(b.view(col.data(), col.size() * 4, 34962), 5126, n, "VEC4");
        int aJnt = b.accessor(b.view(jnt.data(), jnt.size(), 34962), 5121, n, "VEC4");
        int aWgt = b.accessor(b.view(wgt.data(), wgt.size(), 34962), 5121, n, "VEC4", true);
        int aIdx = b.accessor(b.view(p.indices.data(), p.indices.size() * 4, 34963), 5125, p.indices.size(), "SCALAR");
        if (!primsJson.empty()) primsJson += ",";
        primsJson += "{\"attributes\":{\"POSITION\":" + std::to_string(aPos) + ",\"NORMAL\":" + std::to_string(aNor) +
                     ",\"COLOR_0\":" + std::to_string(aCol) + ",\"JOINTS_0\":" + std::to_string(aJnt) +
                     ",\"WEIGHTS_0\":" + std::to_string(aWgt) + "},\"indices\":" + std::to_string(aIdx) +
                     ",\"material\":" + std::to_string(p.material) + "}";
    }
    // Inversas de la pose de reposo (mundo) de cada hueso.
    std::vector<Mat4> world(joints.size());
    std::vector<float> ibm(joints.size() * 16);
    for (size_t i = 0; i < joints.size(); ++i) {
        world[i] = joints[i].parent >= 0 ? world[size_t(joints[i].parent)] * joints[i].local : joints[i].local;
        Mat4 inv = affineInverse(world[i]);
        std::memcpy(&ibm[i * 16], inv.m, 64);
    }
    int aIbm = b.accessor(b.view(ibm.data(), ibm.size() * 4, 0), 5126, joints.size(), "MAT4");

    // Nodos: 0 = raíz de la armadura, 1 = la malla (en la raíz de la escena, como pide glTF
    // para una malla con piel), 2.. = los huesos.
    std::string rootKids;
    for (size_t i = 0; i < joints.size(); ++i)
        if (joints[i].parent < 0) rootKids += (rootKids.empty() ? "" : ",") + std::to_string(2 + i);
    std::string nodes = "{\"name\":\"" + escape(name) + "_Armature\",\"children\":[" + rootKids;
    nodes += "]},{\"name\":\"" + escape(name) + "_Mesh\",\"mesh\":0,\"skin\":0}";
    for (size_t i = 0; i < joints.size(); ++i) {
        nodes += ",{\"name\":\"" + escape(joints[i].name) + "\"";
        // La identidad no se escribe (glTF la da por defecto).
        if (std::memcmp(joints[i].local.m, Mat4::identity().m, 64) != 0) {
            nodes += ",\"matrix\":[";
            for (int k = 0; k < 16; ++k) nodes += (k ? "," : "") + num(joints[i].local.m[k]);
            nodes += "]";
        }
        std::string kids;
        for (size_t c = 0; c < joints.size(); ++c)
            if (joints[c].parent == int(i)) kids += (kids.empty() ? "" : ",") + std::to_string(2 + c);
        if (!kids.empty()) nodes += ",\"children\":[" + kids + "]";
        nodes += "}";
    }
    std::string skinJoints;
    for (size_t i = 0; i < joints.size(); ++i) skinJoints += (i ? "," : "") + std::to_string(2 + i);
    std::string mats;
    for (size_t i = 0; i < materials.size(); ++i) {
        const GlbOutMaterial& m = materials[i];
        mats += (i ? "," : "") + std::string("{\"name\":\"") + escape(m.name) +
                "\",\"pbrMetallicRoughness\":{\"baseColorFactor\":[1,1,1,1],\"metallicFactor\":" +
                (m.name == "Metal" ? "0.8" : "0") + ",\"roughnessFactor\":" + (m.name == "Metal" ? "0.35" : "0.8") + "}";
        if (m.emissive[0] + m.emissive[1] + m.emissive[2] > 0)
            mats += ",\"emissiveFactor\":[" + num(m.emissive[0]) + "," + num(m.emissive[1]) + "," + num(m.emissive[2]) + "]";
        mats += "}";
    }
    while (b.bin.size() % 4) b.bin.push_back(0);
    std::string json = "{\"asset\":{\"version\":\"2.0\",\"generator\":\"unamed_game\"},\"scene\":0,\"scenes\":[{\"nodes\":[0,1]}],"
                       "\"nodes\":[" + nodes + "],\"meshes\":[{\"name\":\"" + escape(name) + "\",\"primitives\":[" + primsJson +
                       "]}],\"skins\":[{\"name\":\"" + escape(name) + "_Armature\",\"inverseBindMatrices\":" +
                       std::to_string(aIbm) + ",\"joints\":[" + skinJoints + "]}],\"materials\":[" + mats +
                       "],\"accessors\":[" + b.accessors + "],\"bufferViews\":[" + b.views +
                       "],\"buffers\":[{\"byteLength\":" + std::to_string(b.bin.size()) + "}]}";
    while (json.size() % 4) json += ' ';
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    auto u32 = [&](uint32_t v) { std::fwrite(&v, 4, 1, f); };
    u32(0x46546C67);  // "glTF"
    u32(2);
    u32(uint32_t(12 + 8 + json.size() + 8 + b.bin.size()));
    u32(uint32_t(json.size()));
    u32(0x4E4F534A);  // JSON
    std::fwrite(json.data(), 1, json.size(), f);
    u32(uint32_t(b.bin.size()));
    u32(0x004E4942);  // BIN
    bool ok = std::fwrite(b.bin.data(), 1, b.bin.size(), f) == b.bin.size();
    std::fclose(f);
    return ok;
}
