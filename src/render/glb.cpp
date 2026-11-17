#include "render/glb.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {

// --- JSON mínimo (lo justo para el capítulo JSON de un .glb) ---------------------------------

struct Json {
    enum Type { Null, Bool, Number, String, Array, Object } type = Null;
    double number = 0;
    bool boolean = false;
    std::string text;
    std::vector<Json> items;                         // Array
    std::vector<std::pair<std::string, Json>> keys;  // Object

    const Json* get(const char* k) const {
        for (const auto& kv : keys)
            if (kv.first == k) return &kv.second;
        return nullptr;
    }
    double num(const char* k, double def) const {
        const Json* v = get(k);
        return v && v->type == Number ? v->number : def;
    }
    int integer(const char* k, int def) const { return int(num(k, def)); }
};

struct JsonParser {
    const char* p;
    const char* end;
    bool ok = true;

    void skip() {
        while (p < end && (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t')) ++p;
    }
    bool eat(char c) {
        skip();
        if (p < end && *p == c) return ++p, true;
        return false;
    }
    std::string str() {
        std::string s;
        if (!eat('"')) return ok = false, s;
        while (p < end && *p != '"') {
            if (*p == '\\' && p + 1 < end) {
                ++p;
                char e = *p++;
                if (e == 'u' && p + 4 <= end) {  // \uXXXX → UTF-8
                    unsigned cp = unsigned(std::strtoul(std::string(p, 4).c_str(), nullptr, 16));
                    p += 4;
                    if (cp < 0x80) s += char(cp);
                    else if (cp < 0x800) s += char(0xC0 | (cp >> 6)), s += char(0x80 | (cp & 0x3F));
                    else s += char(0xE0 | (cp >> 12)), s += char(0x80 | ((cp >> 6) & 0x3F)), s += char(0x80 | (cp & 0x3F));
                } else {
                    s += e == 'n' ? '\n' : e == 't' ? '\t' : e;
                }
            } else {
                s += *p++;
            }
        }
        if (p < end) ++p;
        return s;
    }
    Json value() {
        Json v;
        skip();
        if (p >= end) return ok = false, v;
        if (*p == '{') {
            ++p;
            v.type = Json::Object;
            if (eat('}')) return v;
            do {
                std::string k = str();
                if (!eat(':')) return ok = false, v;
                v.keys.emplace_back(std::move(k), value());
            } while (ok && eat(','));
            if (!eat('}')) ok = false;
        } else if (*p == '[') {
            ++p;
            v.type = Json::Array;
            if (eat(']')) return v;
            do v.items.push_back(value());
            while (ok && eat(','));
            if (!eat(']')) ok = false;
        } else if (*p == '"') {
            v.type = Json::String;
            v.text = str();
        } else if (!std::strncmp(p, "true", 4) || !std::strncmp(p, "false", 5)) {
            v.type = Json::Bool;
            v.boolean = *p == 't';
            p += v.boolean ? 4 : 5;
        } else if (!std::strncmp(p, "null", 4)) {
            p += 4;
        } else {
            char* e = nullptr;
            v.type = Json::Number;
            v.number = std::strtod(p, &e);
            if (e == p) ok = false;
            p = e;
        }
        return v;
    }
};

Mat4 fromTrs(const Json& n) {
    Vec3 t{}, s{1, 1, 1};
    float q[4] = {0, 0, 0, 1};
    if (const Json* a = n.get("translation")) t = {float(a->items[0].number), float(a->items[1].number), float(a->items[2].number)};
    if (const Json* a = n.get("scale")) s = {float(a->items[0].number), float(a->items[1].number), float(a->items[2].number)};
    if (const Json* a = n.get("rotation"))
        for (int i = 0; i < 4; ++i) q[i] = float(a->items[size_t(i)].number);
    float x = q[0], y = q[1], z = q[2], w = q[3];
    Mat4 r = Mat4::identity();
    r.m[0] = 1 - 2 * (y * y + z * z), r.m[1] = 2 * (x * y + z * w), r.m[2] = 2 * (x * z - y * w);
    r.m[4] = 2 * (x * y - z * w), r.m[5] = 1 - 2 * (x * x + z * z), r.m[6] = 2 * (y * z + x * w);
    r.m[8] = 2 * (x * z + y * w), r.m[9] = 2 * (y * z - x * w), r.m[10] = 1 - 2 * (x * x + y * y);
    return translate(t) * r * scale(s);
}

}  // namespace

Mat4 affineInverse(const Mat4& a) {
    // Inversa de la parte 3×3 por cofactores; luego la traslación.
    const float* m = a.m;
    float c00 = m[5] * m[10] - m[6] * m[9], c01 = m[6] * m[8] - m[4] * m[10], c02 = m[4] * m[9] - m[5] * m[8];
    float det = m[0] * c00 + m[1] * c01 + m[2] * c02;
    float k = std::fabs(det) > 1e-12f ? 1.0f / det : 0.0f;
    Mat4 r = Mat4::identity();
    r.m[0] = c00 * k;
    r.m[1] = (m[2] * m[9] - m[1] * m[10]) * k;
    r.m[2] = (m[1] * m[6] - m[2] * m[5]) * k;
    r.m[4] = c01 * k;
    r.m[5] = (m[0] * m[10] - m[2] * m[8]) * k;
    r.m[6] = (m[2] * m[4] - m[0] * m[6]) * k;
    r.m[8] = c02 * k;
    r.m[9] = (m[1] * m[8] - m[0] * m[9]) * k;
    r.m[10] = (m[0] * m[5] - m[1] * m[4]) * k;
    Vec3 t = transformPoint(r, {m[12], m[13], m[14]});
    r.m[12] = -t.x, r.m[13] = -t.y, r.m[14] = -t.z;
    return r;
}

int GlbFile::find(const std::string& name) const {
    for (size_t i = 0; i < nodes.size(); ++i)
        if (nodes[i].name == name) return int(i);
    return -1;
}

bool GlbFile::load(const std::string& path, std::string& error) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return error = "no se puede abrir " + path, false;
    std::fseek(f, 0, SEEK_END);
    long size = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    std::vector<unsigned char> data(size_t(std::max(0L, size)));
    size_t got = std::fread(data.data(), 1, data.size(), f);
    std::fclose(f);
    auto u32 = [&](size_t at) {
        uint32_t v;
        std::memcpy(&v, data.data() + at, 4);
        return v;
    };
    if (got < 20 || std::memcmp(data.data(), "glTF", 4) != 0) return error = path + ": no es un .glb", false;
    // Capítulos: JSON y, después, el binario (BIN).
    size_t at = 12;
    const char* jsonText = nullptr;
    size_t jsonLen = 0;
    const unsigned char* bin = nullptr;
    size_t binLen = 0;
    while (at + 8 <= got) {
        uint32_t len = u32(at), type = u32(at + 4);
        if (at + 8 + len > got) break;
        if (type == 0x4E4F534A) jsonText = reinterpret_cast<const char*>(data.data() + at + 8), jsonLen = len;
        if (type == 0x004E4942) bin = data.data() + at + 8, binLen = len;
        at += 8 + len;
    }
    if (!jsonText) return error = path + ": sin capítulo JSON", false;
    JsonParser jp{jsonText, jsonText + jsonLen};
    Json root = jp.value();
    if (!jp.ok) return error = path + ": JSON mal formado", false;

    const Json* views = root.get("bufferViews");
    const Json* accessors = root.get("accessors");
    // Lee un accessor como floats (VEC3) o como índices.
    auto readAccessor = [&](int idx, int comps, std::vector<float>* floats, std::vector<uint32_t>* ints) -> bool {
        if (!accessors || idx < 0 || size_t(idx) >= accessors->items.size() || !views) return false;
        const Json& a = accessors->items[size_t(idx)];
        const Json& v = views->items[size_t(a.integer("bufferView", 0))];
        size_t count = size_t(a.integer("count", 0)), ctype = size_t(a.integer("componentType", 5126));
        size_t csize = ctype == 5126 || ctype == 5125 ? 4 : ctype == 5123 ? 2 : 1;
        size_t stride = size_t(v.integer("byteStride", 0));
        if (!stride) stride = csize * size_t(comps);
        size_t base = size_t(v.integer("byteOffset", 0)) + size_t(a.integer("byteOffset", 0));
        if (base + (count ? (count - 1) * stride + csize * size_t(comps) : 0) > binLen) return false;
        for (size_t i = 0; i < count; ++i)
            for (int c = 0; c < comps; ++c) {
                const unsigned char* q = bin + base + i * stride + size_t(c) * csize;
                if (floats && ctype == 5126) {
                    float x;
                    std::memcpy(&x, q, 4);
                    floats->push_back(x);
                } else if (floats) {  // enteros normalizados (colores, pesos: siempre van normalizados)
                    uint16_t x = 0;
                    if (ctype == 5123) std::memcpy(&x, q, 2);
                    else x = *q;
                    float scale = ctype == 5123 ? 65535.0f : 255.0f;
                    floats->push_back(float(x) / scale);
                } else if (ctype == 5125) {
                    uint32_t x;
                    std::memcpy(&x, q, 4);
                    ints->push_back(x);
                } else if (ctype == 5123) {
                    uint16_t x;
                    std::memcpy(&x, q, 2);
                    ints->push_back(x);
                } else {
                    ints->push_back(*q);
                }
            }
        return true;
    };

    if (const Json* mats = root.get("materials"))
        for (const Json& m : mats->items) {
            GlbMaterial gm;
            if (const Json* n = m.get("name")) gm.name = n->text;
            if (const Json* pbr = m.get("pbrMetallicRoughness"))
                if (const Json* c = pbr->get("baseColorFactor"))
                    for (int i = 0; i < 4; ++i) gm.base[i] = float(c->items[size_t(i)].number);
            if (const Json* e = m.get("emissiveFactor"))
                for (int i = 0; i < 3; ++i) gm.emissive[i] = float(e->items[size_t(i)].number);
            if (const Json* d = m.get("doubleSided")) gm.doubleSided = d->boolean;
            materials.push_back(gm);
        }
    if (const Json* meshesJ = root.get("meshes"))
        for (const Json& m : meshesJ->items) {
            std::vector<GlbPrimitive> prims;
            if (const Json* ps = m.get("primitives"))
                for (const Json& p : ps->items) {
                    if (p.integer("mode", 4) != 4) continue;  // solo triángulos
                    const Json* attrs = p.get("attributes");
                    if (!attrs || !attrs->get("POSITION")) continue;
                    GlbPrimitive gp;
                    gp.material = p.integer("material", -1);
                    std::vector<float> pos, nor;
                    readAccessor(attrs->integer("POSITION", -1), 3, &pos, nullptr);
                    if (attrs->get("NORMAL")) readAccessor(attrs->integer("NORMAL", -1), 3, &nor, nullptr);
                    for (size_t i = 0; i + 2 < pos.size(); i += 3) gp.positions.push_back({pos[i], pos[i + 1], pos[i + 2]});
                    for (size_t i = 0; i + 2 < nor.size(); i += 3) gp.normals.push_back({nor[i], nor[i + 1], nor[i + 2]});
                    if (attrs->get("COLOR_0")) {  // VEC3 o VEC4: siempre se guarda con alfa
                        int ca = attrs->integer("COLOR_0", -1);
                        bool vec3 = accessors && size_t(ca) < accessors->items.size() &&
                                    accessors->items[size_t(ca)].get("type") &&
                                    accessors->items[size_t(ca)].get("type")->text == "VEC3";
                        std::vector<float> col;
                        readAccessor(ca, vec3 ? 3 : 4, &col, nullptr);
                        for (size_t i = 0; i + (vec3 ? 2 : 3) < col.size(); i += vec3 ? 3 : 4) {
                            gp.colors.insert(gp.colors.end(), {col[i], col[i + 1], col[i + 2], vec3 ? 1.0f : col[i + 3]});
                        }
                    }
                    if (attrs->get("JOINTS_0")) readAccessor(attrs->integer("JOINTS_0", -1), 4, nullptr, &gp.joints);
                    if (attrs->get("WEIGHTS_0")) readAccessor(attrs->integer("WEIGHTS_0", -1), 4, &gp.weights, nullptr);
                    if (p.get("indices")) {
                        readAccessor(p.integer("indices", -1), 1, nullptr, &gp.indices);
                    } else {
                        for (uint32_t i = 0; i < uint32_t(gp.positions.size()); ++i) gp.indices.push_back(i);
                    }
                    prims.push_back(std::move(gp));
                }
            meshes.push_back(std::move(prims));
        }
    if (const Json* nodesJ = root.get("nodes")) {
        nodes.resize(nodesJ->items.size());
        for (size_t i = 0; i < nodes.size(); ++i) {
            const Json& n = nodesJ->items[i];
            GlbNode& g = nodes[i];
            if (const Json* nm = n.get("name")) g.name = nm->text;
            g.mesh = n.integer("mesh", -1);
            g.skin = n.integer("skin", -1);
            if (const Json* mtx = n.get("matrix"))
                for (int k = 0; k < 16; ++k) g.local.m[k] = float(mtx->items[size_t(k)].number);
            else
                g.local = fromTrs(n);
            if (const Json* ch = n.get("children"))
                for (const Json& c : ch->items) g.children.push_back(int(c.number));
        }
        for (size_t i = 0; i < nodes.size(); ++i)
            for (int c : nodes[i].children)
                if (c >= 0 && size_t(c) < nodes.size()) nodes[size_t(c)].parent = int(i);
    }
    if (const Json* skinsJ = root.get("skins"))
        for (const Json& s : skinsJ->items) {
            GlbSkin sk;
            if (const Json* js = s.get("joints"))
                for (const Json& j : js->items) sk.joints.push_back(int(j.number));
            std::vector<float> m;
            if (s.get("inverseBindMatrices")) readAccessor(s.integer("inverseBindMatrices", -1), 16, &m, nullptr);
            for (size_t i = 0; i + 15 < m.size(); i += 16) {
                Mat4 x;
                for (int k = 0; k < 16; ++k) x.m[k] = m[i + size_t(k)];
                sk.inverseBind.push_back(x);
            }
            while (sk.inverseBind.size() < sk.joints.size()) sk.inverseBind.push_back(Mat4::identity());
            skins.push_back(std::move(sk));
        }
    const Json* scenes = root.get("scenes");
    if (scenes && !scenes->items.empty()) {
        if (const Json* sn = scenes->items[size_t(root.integer("scene", 0))].get("nodes"))
            for (const Json& r : sn->items) roots.push_back(int(r.number));
    } else {
        for (size_t i = 0; i < nodes.size(); ++i)
            if (nodes[i].parent < 0) roots.push_back(int(i));
    }
    // Matrices mundo, de la raíz hacia abajo.
    std::vector<int> stack(roots.rbegin(), roots.rend());
    for (int r : roots) nodes[size_t(r)].world = nodes[size_t(r)].local;
    while (!stack.empty()) {
        int i = stack.back();
        stack.pop_back();
        for (int c : nodes[size_t(i)].children) {
            nodes[size_t(c)].world = nodes[size_t(i)].world * nodes[size_t(c)].local;
            stack.push_back(c);
        }
    }
    return true;
}

bool loadPoseFile(const std::string& path, GlbPoseSet& out) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    std::string text;
    char buf[4096];
    for (size_t n; (n = std::fread(buf, 1, sizeof(buf), f)) > 0;) text.append(buf, n);
    std::fclose(f);
    JsonParser parser{text.data(), text.data() + text.size()};
    Json root = parser.value();
    if (!parser.ok || root.type != Json::Object) return false;
    for (const auto& [who, bones] : root.keys) {
        if (who.empty() || who[0] == '_' || bones.type != Json::Object) continue;
        std::vector<std::pair<std::string, Vec3>> pose;
        for (const auto& [bone, r] : bones.keys)
            if (r.type == Json::Array && r.items.size() >= 3)
                pose.push_back({bone, {float(r.items[0].number), float(r.items[1].number), float(r.items[2].number)}});
        out.push_back({who, std::move(pose)});
    }
    return true;
}
