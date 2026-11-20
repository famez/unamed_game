#include "actors/hero_clips.h"

#include "render/glb.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace {

constexpr float kFps = 60.0f;

struct Clip {
    float duration = 0;
    std::vector<ClipSample> frames;
};
Clip gClips[int(HeroClip::Count)];
bool gLoaded = false, gHollinLoaded = false;

const char* const kClipNames[int(HeroClip::Count)] = {"correr", "caminar", "saltar", "combo", "voltereta",
                                                      "correr", "caminar", "combo", "martillazo", "torbellino"};

// Grupos del .glb que forman cada hueso del juego (se encadenan en este orden). El pecho no
// existe en el .glb (la espalda llega hasta el cuello) y las puntas de los pies no se animan.
struct JointNodes {
    int joint;
    const char* nodes[2];
};
const JointNodes kJointNodes[] = {
    {Pelvis, {"Hips"}},        {Spine, {"Spine"}},
    {Neck, {"Neck"}},          {Head, {"Head"}},
    {ShoulderL, {"Shoulder_L", "UpperArm_L"}}, {ElbowL, {"LowerArm_L"}}, {WristL, {"Hand_L"}},
    {ShoulderR, {"Shoulder_R", "UpperArm_R"}}, {ElbowR, {"LowerArm_R"}}, {WristR, {"Hand_R"}},
    {HipL, {"UpperLeg_L"}},    {KneeL, {"LowerLeg_L"}}, {AnkleL, {"Foot_L"}},
    {HipR, {"UpperLeg_R"}},    {KneeR, {"LowerLeg_R"}}, {AnkleR, {"Foot_R"}},
};

Mat4 quatMatrix(float x, float y, float z, float w) {
    Mat4 r = Mat4::identity();
    r.m[0] = 1 - 2 * (y * y + z * z), r.m[1] = 2 * (x * y + z * w), r.m[2] = 2 * (x * z - y * w);
    r.m[4] = 2 * (x * y - z * w), r.m[5] = 1 - 2 * (x * x + z * z), r.m[6] = 2 * (y * z + x * w);
    r.m[8] = 2 * (x * z + y * w), r.m[9] = 2 * (y * z - x * w), r.m[10] = 1 - 2 * (x * x + y * y);
    return r;
}

// Elemento (fila, columna) de la parte 3×3.
float at(const Mat4& m, int row, int col) { return m.m[col * 4 + row]; }

// Valor de una pista en `t`: lineal entre claves (el cuaternión por el camino corto, normalizado).
void sampleChannel(const GlbChannel& ch, float t, float out[4]) {
    int comps = ch.path == GlbChannel::Rotation ? 4 : 3;
    size_t n = ch.times.size();
    size_t i = size_t(std::upper_bound(ch.times.begin(), ch.times.end(), t) - ch.times.begin());
    size_t a = i == 0 ? 0 : i - 1, b = std::min(i, n - 1);
    float u = b > a && !ch.step ? clampf((t - ch.times[a]) / (ch.times[b] - ch.times[a]), 0, 1) : 0.0f;
    const float* A = &ch.values[a * size_t(comps)];
    const float* B = &ch.values[b * size_t(comps)];
    float sign = 1;
    if (comps == 4 && A[0] * B[0] + A[1] * B[1] + A[2] * B[2] + A[3] * B[3] < 0) sign = -1;
    for (int k = 0; k < comps; ++k) out[k] = A[k] + (sign * B[k] - A[k]) * u;
    if (comps == 4) {
        float len = std::sqrt(out[0] * out[0] + out[1] * out[1] + out[2] * out[2] + out[3] * out[3]);
        for (int k = 0; k < 4; ++k) out[k] = len > 1e-8f ? out[k] / len : (k == 3 ? 1.0f : 0.0f);
    }
}

// Euler del juego (M = Ry·Rx·Rz) de una rotación. Hay dos soluciones (x, y, z) y
// (π − x, y + π, z + π): se elige la más cercana a `near` (el fotograma anterior), así un brazo
// que pasa por encima de la cabeza sigue siendo un giro en X y no da la vuelta en Y y Z.
Vec3 eulerYXZ(const Mat4& m, Vec3 near) {
    float sx = clampf(-at(m, 1, 2), -1, 1);
    Vec3 a;
    a.x = std::asin(sx);
    if (std::fabs(sx) < 0.99999f) {
        a.y = std::atan2(at(m, 0, 2), at(m, 2, 2));
        a.z = std::atan2(at(m, 1, 0), at(m, 1, 1));
    } else {
        a.y = std::atan2(-at(m, 2, 0), at(m, 0, 0));
        a.z = 0;
    }
    Vec3 b{wrapAngle(kPi - a.x), wrapAngle(a.y + kPi), wrapAngle(a.z + kPi)};
    auto dist = [&](Vec3 v) {
        float dx = wrapAngle(v.x - near.x), dy = wrapAngle(v.y - near.y), dz = wrapAngle(v.z - near.z);
        return dx * dx + dy * dy + dz * dz;
    };
    return dist(b) < dist(a) ? b : a;
}

// Giro en X de una rotación descompuesta como Rx·Ry·Rz (el orden en que se escribió la
// voltereta: primero la vuelta, luego el resto).
float spinX(const Mat4& m) { return std::atan2(-at(m, 1, 2), at(m, 2, 2)); }

// La pose reflejada en el plano del cuerpo: cada lado toma los giros del otro con Y y Z
// cambiados de signo (Ry·Rx·Rz reflejado en X es R(-y)·Rx·R(-z)).
void mirrorPose(Pose& p) {
    const std::pair<int, int> sides[] = {{ShoulderL, ShoulderR}, {ElbowL, ElbowR}, {WristL, WristR},
                                         {HipL, HipR},           {KneeL, KneeR},   {AnkleL, AnkleR}};
    for (auto [l, r] : sides) std::swap(p.rotation[l], p.rotation[r]);
    for (int j = 0; j < JointCount; ++j) p.rotation[j].y = -p.rotation[j].y, p.rotation[j].z = -p.rotation[j].z;
    p.rootOffset.x = -p.rootOffset.x;
}

// Lee los clips [first, end) del fichero; `mirror`: reflejados.
bool loadClips(const std::string& path, int first, int end, bool mirror, std::string& error) {
    GlbFile g;
    if (!g.load(path, error)) return false;
    int hips = g.find("Hips");
    if (hips < 0) return error = path + ": falta el grupo Hips", false;
    Vec3 hipsRest = translationOf(g.nodes[size_t(hips)].local);
    // Nodos de cada hueso.
    struct Bone {
        int joint;
        int nodes[2];
        int count;
    };
    std::vector<Bone> bones;
    for (const JointNodes& jn : kJointNodes) {
        Bone b{jn.joint, {-1, -1}, 0};
        for (const char* name : jn.nodes) {
            if (!name) continue;
            int i = g.find(name);
            if (i < 0) return error = path + ": falta el grupo " + name, false;
            b.nodes[b.count++] = i;
        }
        bones.push_back(b);
    }
    for (int c = first; c < end; ++c) {
        const GlbAnimation* anim = nullptr;
        for (const GlbAnimation& a : g.animations)
            if (a.name == kClipNames[c]) anim = &a;
        if (!anim || anim->duration <= 0) return error = path + ": falta la animación " + kClipNames[c], false;
        // Pistas de cada nodo.
        std::vector<const GlbChannel*> rot(g.nodes.size(), nullptr), pos(g.nodes.size(), nullptr);
        for (const GlbChannel& ch : anim->channels) {
            if (ch.path == GlbChannel::Rotation) rot[size_t(ch.node)] = &ch;
            if (ch.path == GlbChannel::Translation) pos[size_t(ch.node)] = &ch;
        }
        auto rotation = [&](int node, float t) {
            if (const GlbChannel* ch = rot[size_t(node)]) {
                float q[4];
                sampleChannel(*ch, t, q);
                return quatMatrix(q[0], q[1], q[2], q[3]);
            }
            Mat4 m = g.nodes[size_t(node)].local;  // sin animar: el giro de reposo
            m.m[12] = m.m[13] = m.m[14] = 0;
            return m;
        };
        Clip& clip = gClips[c];
        clip.duration = anim->duration;
        clip.frames.clear();
        bool roll = HeroClip(c) == HeroClip::Roll;
        int n = int(std::ceil(anim->duration * kFps)) + 1;
        for (int f = 0; f < n; ++f) {
            float t = std::min(anim->duration, float(f) / kFps);
            ClipSample s;
            const Pose* prev = f > 0 ? &clip.frames.back().pose : nullptr;
            for (const Bone& b : bones) {
                Mat4 m = rotation(b.nodes[0], t);
                for (int k = 1; k < b.count; ++k) m = m * rotation(b.nodes[k], t);
                if (roll && b.joint == Pelvis) {  // la vuelta aparte: la da la matriz del modelo
                    float sx = spinX(m);
                    if (f > 0) sx = clip.frames.back().spin + wrapAngle(sx - clip.frames.back().spin);
                    s.spin = sx;
                    m = rotateX(-sx) * m;
                }
                s.pose.rotation[b.joint] = eulerYXZ(m, prev ? prev->rotation[b.joint] : Vec3{});
            }
            if (const GlbChannel* ch = pos[size_t(hips)]) {
                float p[4];
                sampleChannel(*ch, t, p);
                s.pose.rootOffset = Vec3{p[0], p[1], p[2]} - hipsRest;
            }
            clip.frames.push_back(s);
        }
        if (mirror)
            for (ClipSample& s : clip.frames) mirrorPose(s.pose);
    }
    return true;
}

}  // namespace

bool loadHeroClips(const std::string& path, std::string& error) {
    gLoaded = loadClips(path, 0, int(HeroClip::HollinRun), false, error);
    return gLoaded;
}

bool loadHollinClips(const std::string& path, std::string& error) {
    gHollinLoaded = loadClips(path, int(HeroClip::HollinRun), int(HeroClip::Count), true, error);
    return gHollinLoaded;
}

bool heroClipsLoaded() { return gLoaded; }
bool hollinClipsLoaded() { return gHollinLoaded; }
float heroClipDuration(HeroClip c) { return gClips[int(c)].duration; }

ClipSample sampleHeroClip(HeroClip c, float t) {
    const Clip& clip = gClips[int(c)];
    if (!(c < HeroClip::HollinRun ? gLoaded : gHollinLoaded) || clip.frames.empty()) return {};
    if (clip.duration > 0) t = std::fmod(std::fmod(t, clip.duration) + clip.duration, clip.duration);
    float f = t * kFps;
    size_t a = std::min(size_t(f), clip.frames.size() - 1), b = std::min(a + 1, clip.frames.size() - 1);
    float u = clampf(f - float(a), 0, 1);
    ClipSample s;
    s.pose = lerpPose(clip.frames[a].pose, clip.frames[b].pose, u, JointCount);
    s.spin = clip.frames[a].spin + (clip.frames[b].spin - clip.frames[a].spin) * u;
    return s;
}
