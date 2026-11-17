#include "game/props.h"

#include "render/debug_draw.h"
#include "render/glb.h"

#include <dirent.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace {

uint8_t linearToSrgb(float lin) {
    lin = std::clamp(lin, 0.0f, 1.0f);
    float s = lin <= 0.0031308f ? lin * 12.92f : 1.055f * std::pow(lin, 1.0f / 2.4f) - 0.055f;
    return uint8_t(std::lround(s * 255.0f));
}

bool startsWith(const std::string& s, const char* prefix) { return s.rfind(prefix, 0) == 0; }

float materialOf(const GlbMaterial& m) {
    if (startsWith(m.name, "Glow") || m.emissive[0] + m.emissive[1] + m.emissive[2] > 0.05f) return float(int(Material::Glow));
    if (startsWith(m.name, "Metal") || startsWith(m.name, "Glass")) return float(int(Material::Metal));
    if (startsWith(m.name, "Stone")) return float(int(Material::Stone));
    if (startsWith(m.name, "Grass")) return float(int(Material::Grass));
    if (startsWith(m.name, "Water")) return float(int(Material::Water));
    return float(int(Material::Cloth));
}

Vec3 transformDir(const Mat4& m, Vec3 d) {
    return {m.m[0] * d.x + m.m[4] * d.y + m.m[8] * d.z, m.m[1] * d.x + m.m[5] * d.y + m.m[9] * d.z,
            m.m[2] * d.x + m.m[6] * d.y + m.m[10] * d.z};
}

// Color de una luz por el nombre (`LUZ_RRGGBB…`); si no lo lleva, cálida.
uint32_t lightColor(const std::string& name) {
    if (name.size() >= 10) {
        std::string hex = name.substr(4, 6);
        if (std::all_of(hex.begin(), hex.end(), [](char c) { return std::isxdigit(static_cast<unsigned char>(c)); })) {
            unsigned long v = std::strtoul(hex.c_str(), nullptr, 16);
            return rgba(uint8_t(v >> 16), uint8_t(v >> 8), uint8_t(v));
        }
    }
    return rgba(255, 214, 150);
}

}  // namespace

PropLibrary& propLibrary() {
    static PropLibrary lib;
    return lib;
}

int PropLibrary::find(const std::string& name) const {
    for (size_t i = 0; i < defs_.size(); ++i)
        if (defs_[i].name == name) return int(i);
    return -1;
}

bool PropLibrary::loadFile(const std::string& path, const std::string& name, std::string& error) {
    GlbFile g;
    if (!g.load(path, error)) return false;
    PropDef d;
    d.name = name;
    Vec3 lo{1e9f, 1e9f, 1e9f}, hi{-1e9f, -1e9f, -1e9f};
    for (const GlbNode& n : g.nodes) {
        if (startsWith(n.name, "LUZ")) d.lights.push_back({translationOf(n.world), lightColor(n.name)});
        if (n.mesh < 0) continue;
        bool collider = startsWith(n.name, "COL");
        Vec3 clo{1e9f, 1e9f, 1e9f}, chi{-1e9f, -1e9f, -1e9f};
        for (const GlbPrimitive& p : g.meshes[size_t(n.mesh)]) {
            if (collider) {
                for (Vec3 v : p.positions) {
                    Vec3 w = transformPoint(n.world, v);
                    clo = {std::min(clo.x, w.x), std::min(clo.y, w.y), std::min(clo.z, w.z)};
                    chi = {std::max(chi.x, w.x), std::max(chi.y, w.y), std::max(chi.z, w.z)};
                }
                continue;
            }
            GlbMaterial mat;
            if (p.material >= 0 && size_t(p.material) < g.materials.size()) mat = g.materials[size_t(p.material)];
            float m = materialOf(mat);
            uint32_t base = uint32_t(d.verts.size());
            for (size_t i = 0; i < p.positions.size(); ++i) {
                SkinVertex v{};
                Vec3 w = transformPoint(n.world, p.positions[i]);
                Vec3 nn = i < p.normals.size() ? transformDir(n.world, p.normals[i]) : Vec3{0, 1, 0};
                nn = length(nn) > 1e-6f ? normalize(nn) : Vec3{0, 1, 0};
                v.pos[0] = w.x, v.pos[1] = w.y, v.pos[2] = w.z;
                v.normal[0] = nn.x, v.normal[1] = nn.y, v.normal[2] = nn.z;
                float c[3];
                for (int k = 0; k < 3; ++k)
                    c[k] = (p.colors.size() >= (i + 1) * 4 ? p.colors[i * 4 + size_t(k)] : 1.0f) * mat.base[k];
                v.color = rgba(linearToSrgb(c[0]), linearToSrgb(c[1]), linearToSrgb(c[2]));
                v.weights[0] = 255;  // todo en el hueso 0: la matriz de la instancia
                v.material = m + 16.0f;  // escenario: le afectan el croma, la oscuridad y las salpicaduras
                d.verts.push_back(v);
                lo = {std::min(lo.x, w.x), std::min(lo.y, w.y), std::min(lo.z, w.z)};
                hi = {std::max(hi.x, w.x), std::max(hi.y, w.y), std::max(hi.z, w.z)};
            }
            if (p.indices.empty())
                for (size_t i = 0; i < p.positions.size(); ++i) d.indices.push_back(base + uint32_t(i));
            else
                for (uint32_t ix : p.indices) d.indices.push_back(base + ix);
        }
        if (collider && clo.x <= chi.x) d.colliders.push_back({clo, chi});
    }
    if (d.verts.empty()) return error = path + ": no tiene ninguna malla visible", false;
    d.bounds = {lo, hi};
    int existing = find(name);
    if (existing >= 0) {
        d.mesh = -1;  // hay que volver a subirla
        defs_[size_t(existing)] = std::move(d);
    } else {
        defs_.push_back(std::move(d));
    }
    return true;
}

int PropLibrary::loadDir(const std::string& dir) {
    DIR* dp = opendir(dir.c_str());
    if (!dp) return 0;
    std::vector<std::string> files;
    while (dirent* e = readdir(dp)) {
        std::string f = e->d_name;
        if (f.size() > 4 && f.compare(f.size() - 4, 4, ".glb") == 0) files.push_back(f);
    }
    closedir(dp);
    std::sort(files.begin(), files.end());
    int loaded = 0;
    for (const std::string& f : files) {
        std::string error;
        if (loadFile(dir + "/" + f, f.substr(0, f.size() - 4), error))
            ++loaded;
        else
            std::fprintf(stderr, "%s\n", error.c_str());
    }
    return loaded;
}
