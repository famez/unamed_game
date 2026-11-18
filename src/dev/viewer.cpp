#include "dev/viewer.h"

#include "actors/character.h"
#include "actors/creature.h"
#include "actors/enemy.h"
#include "actors/physics.h"
#include "actors/skeleton.h"
#include "core/gl.h"
#include "core/platform.h"
#include "game/game.h"
#include "game/props.h"
#include "render/glb.h"
#include "render/hero_model.h"
#include "render/models.h"

#include <dirent.h>
#include <sys/stat.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <memory>
#include <string>
#include <vector>

namespace {

constexpr float kStep = 1.0f / 60.0f;

// Lo que se ha leído del fichero.
struct Loaded {
    enum class Kind { None, Hero, Character, Prop, Gallery, Creature } kind = Kind::None;
    HeroGlbModel hero;        // Vela, Hollín o una protagonista nueva (piezas por hueso)
    BuiltModel body;          // personaje con armadura
    ModelId id = ModelId::Hero;
    bool humanoid = false;    // esqueleto de la protagonista: se anima con Character
    std::shared_ptr<CreatureRig> rig;  // criatura: su esqueleto propio (el del .glb)
    int creature = -1;        // qué criatura del bestiario es (-1: otra, sin animación)
    bool shadowLook = false;  // contorno violeta (Hollín) en lugar de dorado
    std::string what;         // qué es, para el panel
    size_t triangles = 0;
    int bones = 0;
    std::vector<std::string> materials;  // nombre → cómo lo entiende el juego
    std::vector<int> props;              // pieza (una) o galería (todas), en propLibrary()
    std::vector<Vec3> spots;             // dónde va cada una en la galería
    int colliders = 0, lights = 0;
    bool still() const { return kind == Kind::Prop || kind == Kind::Gallery; }  // sin animación
    Vec3 center, extent{1, 1, 1};        // lo que ocupa (para la cámara)
};

bool isDir(const std::string& path) {
    struct stat st;
    return stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

// Galería: todas las piezas de la carpeta en filas, de la más pequeña a la más grande.
void layoutGallery(Loaded& l) {
    const PropLibrary& lib = propLibrary();
    std::sort(l.props.begin(), l.props.end(), [&](int a, int b) {
        Vec3 ea = lib[size_t(a)].bounds.max - lib[size_t(a)].bounds.min, eb = lib[size_t(b)].bounds.max - lib[size_t(b)].bounds.min;
        return ea.x * ea.z * ea.y < eb.x * eb.z * eb.y;
    });
    float x = 0, z = 0, rowDepth = 0, maxX = 0;
    const float rowWidth = 70;
    for (int i : l.props) {
        const AABB& b = lib[size_t(i)].bounds;
        float w = b.max.x - b.min.x, d = b.max.z - b.min.z;
        if (x > 0 && x + w > rowWidth) x = 0, z -= rowDepth + 3, rowDepth = 0;
        l.spots.push_back({x - b.min.x, 0, z - b.max.z});
        x += w + 3;
        maxX = std::max(maxX, x);
        rowDepth = std::max(rowDepth, d);
    }
    l.center = {maxX * 0.5f, 2, (z - rowDepth) * 0.5f};
    l.extent = {maxX * 0.5f, 6, (rowDepth - z) * 0.5f};
}

// Último cambio de los .glb de una carpeta (o del fichero).
long long newestNs(const std::string& path);

std::string stemOf(const std::string& path) {
    size_t slash = path.find_last_of('/');
    std::string s = slash == std::string::npos ? path : path.substr(slash + 1);
    size_t dot = s.rfind('.');
    return dot == std::string::npos ? s : s.substr(0, dot);
}

const char* rigName(ModelId id) {
    switch (id) {
        case ModelId::Dazzled: return "Deslumbrado";
        case ModelId::Brute: return "Coloso";
        case ModelId::Watcher: return "Vigía";
        case ModelId::Hound: return "Sabueso";
        case ModelId::Crab: return "Cangrejo de cristal";
        case ModelId::Ilse: return "protagonista (Ilse)";
        default: return "protagonista";
    }
}

std::string materialRole(const GlbMaterial& m, bool hero) {
    if (hero) return m.name;  // la protagonista reparte por grupos, no por material
    bool glow = m.name.rfind("Glow", 0) == 0 || m.emissive[0] + m.emissive[1] + m.emissive[2] > 0.05f;
    auto is = [&](const char* p) { return m.name.rfind(p, 0) == 0; };
    const char* role = glow ? "brilla" : is("Metal") || is("Glass") ? "metal" : is("Hair") ? "pelo" : is("Stone") ? "piedra"
                     : is("Grass") ? "hierba" : is("Water") ? "agua" : "tela";
    return m.name + " · " + role;
}

// Carga `path`: primero como personaje conocido por su nombre, luego como protagonista y, si
// no, con el esqueleto de cualquier personaje cuyos huesos tenga.
bool load(Renderer& r, const std::string& path, Loaded& out, std::string& error) {
    Loaded l;
    PropLibrary& lib = propLibrary();
    if (isDir(path)) {  // galería de piezas
        if (lib.loadDir(path) == 0) return error = path + ": no hay ninguna pieza .glb que se pueda leer", false;
        uploadPropMeshes(r);
        DIR* dp = opendir(path.c_str());
        while (dirent* e = dp ? readdir(dp) : nullptr) {
            std::string f = e->d_name;
            if (f.size() > 4 && f.compare(f.size() - 4, 4, ".glb") == 0) {
                int i = lib.find(f.substr(0, f.size() - 4));
                if (i >= 0) l.props.push_back(i);
            }
        }
        if (dp) closedir(dp);
        l.kind = Loaded::Kind::Gallery;
        l.what = "galería de piezas del escenario";
        for (int i : l.props) {
            l.triangles += lib[size_t(i)].indices.size() / 3;
            l.colliders += int(lib[size_t(i)].colliders.size());
            l.lights += int(lib[size_t(i)].lights.size());
        }
        layoutGallery(l);
        out = std::move(l);
        return true;
    }
    GlbFile g;
    if (!g.load(path, error)) return false;
    std::string stem = stemOf(path);
    if (g.skins.empty() && g.find("Hips") < 0) {  // sin esqueleto: una pieza del escenario
        if (!lib.loadFile(path, stem, error)) return false;
        uploadPropMeshes(r);
        int i = lib.find(stem);
        const PropDef& d = lib[size_t(i)];
        l.kind = Loaded::Kind::Prop;
        l.props = {i};
        l.spots = {{0, 0, 0}};
        l.what = "pieza del escenario";
        l.colliders = int(d.colliders.size());
        l.lights = int(d.lights.size());
        l.center = d.bounds.center();
        l.extent = d.bounds.halfExtents();
        for (const auto& mesh : g.meshes)
            for (const GlbPrimitive& p : mesh) l.triangles += p.indices.empty() ? p.positions.size() / 3 : p.indices.size() / 3;
        for (const GlbMaterial& m : g.materials) l.materials.push_back(materialRole(m, false));
        out = std::move(l);
        return true;
    }
    ModelId id;
    bool wolf = false;
    std::vector<ModelId> tries;
    if (glbCharacterId(stem, id, wolf)) tries.push_back(id);
    // Criatura con su propio esqueleto: las del bestiario por su nombre y, si no encaja con ningún
    // esqueleto del juego, cualquier otra (quieta).
    auto asCreature = [&](int which) {
        auto rig = std::make_shared<CreatureRig>();
        std::string e;
        if (!creatureRigFromGlb(g, *rig, e) || !loadCharacterGlb(r, path, rig->joints, rig->count, l.body, e)) return error = path + ": " + e, false;
        l.kind = Loaded::Kind::Creature;
        l.rig = rig;
        l.creature = which;
        l.bones = rig->count;
        l.what = which >= 0 ? std::string("criatura del bestiario: ") + creatureName(CreatureKind(which)) : std::string("esqueleto propio (sin animación)");
        Vec3 lo{1e9f, 1e9f, 1e9f}, hi{-1e9f, -1e9f, -1e9f};
        for (const auto& mesh : g.meshes)
            for (const GlbPrimitive& p : mesh)
                for (const Vec3& v : p.positions) lo = {std::min(lo.x, v.x), std::min(lo.y, v.y), std::min(lo.z, v.z)}, hi = {std::max(hi.x, v.x), std::max(hi.y, v.y), std::max(hi.z, v.z)};
        float sc = which >= 0 ? creatureScale(CreatureKind(which)) : 1.0f;
        l.center = (lo + hi) * (0.5f * sc);
        l.extent = (hi - lo) * (0.5f * sc);
        return true;
    };
    int which = -1;
    for (int k = 0; k < int(CreatureKind::Count); ++k)
        if (stem == creatureFile(CreatureKind(k))) which = k;
    if (which >= 0 && !asCreature(which)) return false;
    std::string heroError;
    if (which >= 0) {
    } else if (tries.empty() && loadHeroGlb(r, path, l.hero, heroError)) {
        l.kind = Loaded::Kind::Hero;
        l.humanoid = true;
        l.shadowLook = stem.find("hollin") != std::string::npos || stem.find("sombra") != std::string::npos;
        l.what = "protagonista (piezas por hueso, ficha FICHA_VELA_HOLLIN.md)";
        l.bones = JointCount;
    } else {
        for (ModelId t : {ModelId::Dazzled, ModelId::Brute, ModelId::Watcher, ModelId::Hound, ModelId::Ilse})
            if (std::find(tries.begin(), tries.end(), t) == tries.end()) tries.push_back(t);
        std::string firstError;
        for (ModelId t : tries) {
            const ModelDef& def = modelDef(t);
            std::string e;
            if (loadCharacterGlb(r, path, def.rig, def.jointCount, l.body, e)) {
                l.kind = Loaded::Kind::Character;
                l.id = t;
                l.humanoid = def.rig == kSkeleton;
                l.what = std::string("personaje con armadura, esqueleto del ") + rigName(t);
                if (t == ModelId::Ilse) l.what = "personaje con armadura, esqueleto de la protagonista";
                l.bones = def.jointCount;
                break;
            }
            if (firstError.empty()) firstError = e;
        }
        if (l.kind == Loaded::Kind::None && !g.skins.empty() && asCreature(-1)) firstError.clear(), heroError.clear();
        if (l.kind == Loaded::Kind::None) {
            error = !heroError.empty() && firstError.empty() ? heroError : firstError;
            if (!heroError.empty()) error += "\n(como protagonista: " + heroError + ")";
            return false;
        }
    }
    for (const auto& mesh : g.meshes)
        for (const GlbPrimitive& p : mesh) l.triangles += p.indices.empty() ? p.positions.size() / 3 : p.indices.size() / 3;
    for (const GlbMaterial& m : g.materials) l.materials.push_back(materialRole(m, l.kind == Loaded::Kind::Hero));
    out = std::move(l);
    return true;
}

// Animaciones del visor.
const char* const kHumanAnims[] = {"Quieta", "Andar", "Correr", "Combo", "Saltar", "Esquivar", "Guardia"};
const char* const kEnemyAnims[] = {"Quieto", "Persigue", "Ataque", "Aturdido", "Aparece", "Muere"};
// Criaturas: lo especial de cada una (saltar de tejado, volar, cavar, esconderse, encender una farola...).
const char* const kCreatureAnims[] = {"Quieta", "Anda", "Especial"};

struct Orbit {
    float yaw = 2.6f, pitch = 0.18f, distance = 3.6f, height = 0.9f;
};

Mat4 basisY(Vec3 d) {
    Vec3 y = normalize(d);
    Vec3 ref = std::fabs(y.y) < 0.9f ? Vec3{0, 1, 0} : Vec3{1, 0, 0};
    Vec3 x = normalize(cross(ref, y)), z = cross(x, y);
    Mat4 m = Mat4::identity();
    m.m[0] = x.x; m.m[1] = x.y; m.m[2] = x.z;
    m.m[4] = y.x; m.m[5] = y.y; m.m[6] = y.z;
    m.m[8] = z.x; m.m[9] = z.y; m.m[10] = z.z;
    return m;
}

void savePpm(const char* path, int w, int h) {
    std::vector<unsigned char> px(size_t(w) * size_t(h) * 3);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, w, h, GL_RGB, GL_UNSIGNED_BYTE, px.data());
    FILE* f = std::fopen(path, "wb");
    if (!f) return;
    std::fprintf(f, "P6\n%d %d\n255\n", w, h);
    for (int y = h - 1; y >= 0; --y) std::fwrite(px.data() + size_t(y) * size_t(w) * 3, 1, size_t(w) * 3, f);
    std::fclose(f);
}

long long modifiedNs(const std::string& path) {
    struct stat st;
    if (stat(path.c_str(), &st) != 0) return -1;
    return (long long)st.st_mtim.tv_sec * 1000000000LL + st.st_mtim.tv_nsec;
}

long long newestNs(const std::string& path) {
    if (!isDir(path)) return modifiedNs(path);
    long long newest = modifiedNs(path);  // la carpeta cambia al añadir o quitar ficheros
    if (DIR* dp = opendir(path.c_str())) {
        while (dirent* e = readdir(dp)) {
            std::string f = e->d_name;
            if (f.size() > 4 && f.compare(f.size() - 4, 4, ".glb") == 0) newest = std::max(newest, modifiedNs(path + "/" + f));
        }
        closedir(dp);
    }
    return newest;
}

}  // namespace

int runViewer(const char* pathArg, Renderer& renderer, DebugDraw& lines, UiLayer& ui, int argc, char** argv) {
    const std::string path = pathArg;
    int anim = 0;
    bool startNight = false, closeUp = false;  // --cerca: la cámara en la cara
    const char* shotPath = nullptr;
    float shotTime = 0;
    int shotCount = 1, shotIndex = 0;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--anim") == 0 && i + 1 < argc) anim = std::max(0, std::atoi(argv[i + 1]) - 1);
        if (std::strcmp(argv[i], "--noche") == 0) startNight = true;
        if (std::strcmp(argv[i], "--cerca") == 0) closeUp = true;
        if (std::strcmp(argv[i], "--screenshot") == 0 && i + 2 < argc) {
            shotPath = argv[i + 1];
            shotTime = float(std::atof(argv[i + 2]));
            if (i + 3 < argc && argv[i + 3][0] != '-') shotCount = std::max(1, std::atoi(argv[i + 3]));
        }
    }

    Loaded model;
    std::string error, status;
    double statusTime = -10;
    bool ok = load(renderer, path, model, error);
    if (!ok) std::fprintf(stderr, "%s\n", error.c_str());
    long long stamp = newestNs(path), pendingStamp = stamp;
    double pendingSince = 0;

    // Estado de la animación.
    World world;  // suelo infinito
    Character hero;
    Enemy enemy;
    float animTime = 0, simTime = 0;
    auto resetActors = [&]() {
        hero = Character();
        hero.teleport({0, 0, 0}, 0);
        if (model.kind == Loaded::Kind::Character && !model.humanoid) {
            EnemyKind kind = EnemyKind(int(model.id) - 1);
            const EnemyDef& d = enemyDef(kind);
            enemy = makeEnemy(kind, {0, d.flying ? d.hoverHeight - d.height * 0.5f : 0.0f, 0}, 0, 1.3f);
            enemy.state = AiState::Idle;
        }
        animTime = 0;
    };
    resetActors();
    auto animsOf = [](const Loaded& l) {
        if (l.kind == Loaded::Kind::Creature) return l.creature >= 0 ? int(std::size(kCreatureAnims)) : 0;
        return l.still() || l.kind == Loaded::Kind::None ? 0 : l.humanoid ? int(std::size(kHumanAnims)) : int(std::size(kEnemyAnims));
    };
    CreatureMotion motion;
    int animCount = animsOf(model);
    anim = std::max(0, std::min(anim, animCount - 1));

    // Altura a la que mira la cámara: el centro del cuerpo (los voladores, a su altura de vuelo).
    auto subjectHeight = [&]() {
        if (model.still() || model.kind == Loaded::Kind::Creature) return std::max(0.3f, model.center.y);
        if (model.humanoid || model.kind == Loaded::Kind::None) return 0.9f;
        const EnemyDef& d = enemyDef(EnemyKind(int(model.id) - 1));
        return d.flying ? d.hoverHeight : d.height * 0.55f;
    };
    // Distancia según el tamaño: que quepa entero.
    auto frame = [&](Orbit& o) {
        o.height = subjectHeight();
        float size = model.humanoid || model.kind == Loaded::Kind::None ? 1.8f
                     : model.still() || model.kind == Loaded::Kind::Creature ? length(model.extent) * 1.4f
                                                                         : enemyDef(EnemyKind(int(model.id) - 1)).height;
        o.distance = clampf(size * 2.0f, 2.4f, 140.0f);
        if (model.kind == Loaded::Kind::Gallery) o.pitch = 0.45f, o.yaw = kPi, o.distance = std::min(o.distance, 42.0f);
    };
    Orbit cam;
    frame(cam);
    if (closeUp && model.humanoid) cam.height = 1.55f, cam.distance = 0.9f, cam.pitch = 0.05f;
    Vec3 focus{0, 0, 0}, pan{};
    bool paused = false, skeleton = false, night = startNight;
    float lastMouseX = 0, lastMouseY = 0;
    Input input;
    const double start = platform::timeSeconds();
    double prev = start, acc = 0;
    platform::setMouseCaptured(false);
    platform::setTitle(("Visor · " + stemOf(path) + ".glb").c_str());

    for (bool quit = false; !quit;) {
        double now = platform::timeSeconds();
        double frameDt = std::min(0.1, now - prev);
        prev = now;
        if (!platform::pumpEvents(input)) break;
        if (input.wasPressed(Key::Escape)) break;

        // Recarga al cambiar el fichero (espera a que deje de cambiar: Blender lo escribe a trozos).
        long long m = newestNs(path);
        if (m != pendingStamp) pendingStamp = m, pendingSince = now;
        bool reload = input.wasPressed(Key::R) || (m >= 0 && m != stamp && now - pendingSince > 0.4);
        if (reload) {
            stamp = m;
            Loaded fresh;
            std::string e;
            int prevCount = animCount;
            if (load(renderer, path, fresh, e)) {
                model = std::move(fresh);
                ok = true;
                error.clear();
                resetActors();
                animCount = animsOf(model);
                if (animCount != prevCount || model.still()) anim = 0, frame(cam);
                char t[32];
                std::time_t tt = std::time(nullptr);
                std::strftime(t, sizeof(t), "%H:%M:%S", std::localtime(&tt));
                status = std::string("Recargado a las ") + t;
            } else {
                error = e;  // se queda el último que se pudo leer
                std::fprintf(stderr, "%s\n", e.c_str());
            }
            statusTime = now;
        }

        // Controles.
        for (int k = 0; k < animCount && k < 8; ++k)
            if (input.wasPressed(Key(int(Key::N1) + k))) anim = k, animTime = 0, resetActors();
        if (input.wasPressed(Key::Tab)) anim = (anim + 1) % animCount, animTime = 0, resetActors();
        if (input.wasPressed(Key::Space)) paused = !paused;
        if (input.wasPressed(Key::F)) skeleton = !skeleton;
        if (input.wasPressed(Key::V)) night = !night;
        if (input.wasPressed(Key::C)) cam = Orbit{}, frame(cam), pan = {};
        float mdx = input.mouseX - lastMouseX, mdy = input.mouseY - lastMouseY;
        lastMouseX = input.mouseX, lastMouseY = input.mouseY;
        if (input.mouseLeft) cam.yaw -= mdx * 0.008f, cam.pitch = clampf(cam.pitch + mdy * 0.006f, -0.6f, 1.45f);
        if (input.mouseRight || input.mouseMiddle) cam.height = clampf(cam.height + mdy * 0.004f * cam.distance, -0.5f, 4.0f);
        cam.distance = clampf(cam.distance * std::pow(0.88f, input.wheel), 0.6f, 160.0f);
        // En la galería, WASD lleva la cámara por encima de las piezas.
        if (model.kind == Loaded::Kind::Gallery) {
            Vec3 fwd{std::sin(cam.yaw), 0, std::cos(cam.yaw)}, right{-std::cos(cam.yaw), 0, std::sin(cam.yaw)};
            float k = float(frameDt) * std::max(6.0f, cam.distance * 0.8f);
            pan += fwd * (k * ((input.isDown(Key::W) ? 1.0f : 0.0f) - (input.isDown(Key::S) ? 1.0f : 0.0f)));
            pan += right * (k * ((input.isDown(Key::D) ? 1.0f : 0.0f) - (input.isDown(Key::A) ? 1.0f : 0.0f)));
        }
        float turn = (input.isDown(Key::Right) ? 1.0f : 0.0f) - (input.isDown(Key::Left) ? 1.0f : 0.0f);
        cam.yaw += turn * float(frameDt) * 1.8f;
        float tilt = (input.isDown(Key::Up) ? 1.0f : 0.0f) - (input.isDown(Key::Down) ? 1.0f : 0.0f);
        cam.pitch = clampf(cam.pitch + tilt * float(frameDt) * 1.2f, -0.6f, 1.45f);

        // Simulación a pasos fijos, como en el juego.
        acc += paused || !ok ? 0.0 : frameDt;
        while (acc >= kStep) {
            acc -= kStep;
            animTime += kStep;
            simTime += kStep;
            if (model.still()) {
            } else if (model.kind == Loaded::Kind::Creature) {
                // Como las mueve el juego (game/creatures.cpp), en el sitio.
                CreatureKind ck = CreatureKind(std::max(0, model.creature));
                float walk = ck == CreatureKind::Mechita ? 2.0f : ck == CreatureKind::Hollinete ? 0.9f : ck == CreatureKind::Caracol ? 0.3f
                           : ck == CreatureKind::Larguero ? 1.3f : ck == CreatureKind::Ballena ? 2.6f : 0.0f;
                float rate = ck == CreatureKind::Mechita ? 12.0f : ck == CreatureKind::Hollinete ? 7.0f : ck == CreatureKind::Caracol ? 1.8f
                           : ck == CreatureKind::Larguero ? 2.9f : 1.0f;
                motion = CreatureMotion{};
                motion.time = simTime;
                motion.look = anim == 0 ? 0.8f * std::sin(simTime * 0.5f) : 0.0f;
                if (anim == 1 || ck == CreatureKind::Ballena) motion.speed = walk;
                static float phase = 0;
                if (anim == 2) {
                    float u = std::fmod(animTime, 2.4f);
                    switch (ck) {
                        case CreatureKind::Mechita:
                        case CreatureKind::Caracol: motion.hide = smoothstep01(std::sin(u / 2.4f * kPi)); break;
                        case CreatureKind::Hollinete: motion.hop = u < 1.0f ? u : -1.0f; break;
                        case CreatureKind::Buho: motion.hop = 0.5f, rate = 16.0f; break;
                        case CreatureKind::Topo: motion.hide = 1.0f, motion.look = 0.5f * std::sin(simTime * 0.7f); break;
                        case CreatureKind::Larguero: motion.lighting = u < 2.2f ? u : -1.0f; break;
                        default: break;
                    }
                }
                phase += (motion.speed > 0 || motion.hop >= 0 ? rate : 0.0f) * kStep;
                motion.phase = phase;
                for (int k = 0; k < model.rig->count; ++k) enemy.prevJoints[k] = enemy.joints[k];
                Pose pose;
                if (model.creature >= 0) poseCreature(ck, *model.rig, motion, pose);
                float sc = model.creature >= 0 ? creatureScale(ck) : 1.0f;
                computeWorldMatrices(model.rig->joints, model.rig->count, pose, scale({sc, sc, sc}), enemy.joints);
            } else if (model.humanoid) {
                Vec3 dir{};
                bool jump = false;
                float circle = animTime * 0.55f;  // da vueltas en un círculo amplio
                Vec3 around{std::cos(circle), 0, -std::sin(circle)};
                hero.setSprint(anim == 2);
                hero.setGuard(anim == 6);
                hero.setArmed(anim == 3 || anim == 6);
                hero.setAlert(anim == 3 || anim == 6);
                switch (anim) {
                    case 1: dir = around * 0.35f; break;
                    case 2: dir = around; break;
                    case 3:
                        if (std::fmod(animTime, 0.3f) < kStep) hero.attack(hero.yaw, 0);
                        break;
                    case 4: jump = std::fmod(animTime, 1.4f) < kStep; break;
                    case 5:
                        if (std::fmod(animTime, 1.2f) < kStep) hero.dodge(hero.forward());
                        break;
                    default: break;
                }
                hero.update(kStep, dir, jump, world, hero.yaw);
                if (anim == 5 && length(hero.body.pos) > 6.0f) hero.teleport({0, 0, 0}, hero.yaw + kPi);
                if (anim == 3 && length(hero.body.pos) > 6.0f) hero.teleport({0, 0, 0}, hero.yaw + kPi);
            } else {
                const EnemyDef& d = enemy.def();
                AiState want = AiState::Idle;
                switch (anim) {
                    case 1: want = AiState::Chase; break;
                    case 2: {  // preparación, golpe, recuperación y una pausa
                        float cycle = d.windup + d.attackTime + d.recover + 0.8f, t = std::fmod(animTime, cycle);
                        want = t < d.windup                              ? AiState::Windup
                               : t < d.windup + d.attackTime             ? AiState::Attack
                               : t < d.windup + d.attackTime + d.recover ? AiState::Recover
                                                                         : AiState::Idle;
                        break;
                    }
                    case 3: want = AiState::Stagger; break;
                    case 4: want = std::fmod(animTime, 2.0f) < 1.2f ? AiState::Spawning : AiState::Idle; break;
                    case 5: want = std::fmod(animTime, 1.6f) < 0.8f ? AiState::Dying : AiState::Idle; break;
                    default: break;
                }
                if (want != enemy.state) enemy.state = want, enemy.stateTime = 0, enemy.blendTime = 0, enemy.attackHit = false;
                enemy.stateTime += kStep;
                float moving = enemy.state == AiState::Chase ? 1.0f : 0.25f;
                enemy.phase += (d.flying ? 14.0f : 2.0f + moving * 3.0f) * kStep;  // como en enemy_ai.cpp
                for (int k = 0; k < d.jointCount; ++k) enemy.prevJoints[k] = enemy.joints[k];
                animateEnemy(enemy, simTime, kStep);
            }
        }
        float alpha = float(acc / kStep);

        // Escena.
        int w = platform::width(), h = platform::height();
        renderer.setNight(night ? 1.0f : 0.0f);
        renderer.setDarkness(0);
        renderer.setWorldColor(1);
        renderer.setFog(0.4f);
        Vec3 subject = model.still() ? Vec3{model.center.x, 0, model.center.z} + pan
                       : model.humanoid ? hero.interpolatedPos(alpha) : Vec3{};
        focus = focus + (subject - focus) * std::min(1.0f, float(frameDt) * 6.0f);
        renderer.setScenery(true);
        renderer.box(Mat4::identity(), {focus.x, -0.05f, focus.z}, {150, 0.05f, 150}, rgba(92, 94, 100), Material::Stone);
        renderer.setScenery(false);
        if (night) renderer.light(focus + Vec3{1.5f, 2.5f, 1.5f}, Vec3{1.0f, 0.9f, 0.75f} * 2.0f, 9.0f);

        Mat4 j[kMaxJoints];
        const JointDef* rig = kSkeleton;
        int jointCount = JointCount;
        if (ok && model.humanoid) {
            hero.interpolatedJoints(alpha, j);
            if (anim == 0) inhabitantIdleJoints(stemOf(path), Mat4::identity(), simTime, j);  // su pose de reposo, si tiene
            Vec3 lagW = hero.hairLag();
            const float* hm = j[Head].m;
            Vec3 lag{hm[0] * lagW.x + hm[1] * lagW.y + hm[2] * lagW.z, hm[4] * lagW.x + hm[5] * lagW.y + hm[6] * lagW.z,
                     hm[8] * lagW.x + hm[9] * lagW.y + hm[10] * lagW.z};
            if (model.kind == Loaded::Kind::Hero) {
                uint32_t line = model.shadowLook ? rgba(34, 18, 56) : rgba(120, 84, 30);
                float run = clampf(hero.speed / Character::kSprintSpeed, 0, 1);
                drawHeroGlb(renderer, model.hero, j, lag, run, simTime, 0, line, 1);
                if (model.hero.prop >= 0) {  // el arma: en la mano o a la espalda
                    Mat4 bone = hero.weaponInHand()
                                    ? j[WristR] * translate(kGripLocal) * rotateX(kPi * 0.5f)
                                    : j[Chest] * translate({-0.1f, 0.25f, -0.14f}) *
                                          basisY(normalize(Vec3{0.45f, -1.0f, -0.05f})) * rotateX(-kPi * 0.5f) *
                                          translate(kGripLocal) * rotateX(kPi * 0.5f);
                    renderer.skinned(model.hero.prop, &bone, 1, 0, line);
                }
            } else {
                Mat4 skin[kMaxJoints];
                skinMatrices(model.body, j, skin);
                renderer.skinned(model.body.mesh, skin, model.body.jointCount, 0, rgba(20, 20, 30));
            }
        } else if (ok && model.still()) {
            const PropLibrary& lib = propLibrary();
            for (size_t k = 0; k < model.props.size(); ++k) {
                const PropDef& d = lib[size_t(model.props[k])];
                renderer.prop(d.mesh, translate(model.spots[k]));
                for (const PropLight& l : d.lights) {
                    Vec3 c{float(l.color & 0xFF) / 255.0f, float((l.color >> 8) & 0xFF) / 255.0f, float((l.color >> 16) & 0xFF) / 255.0f};
                    renderer.light(model.spots[k] + l.pos, c * (night ? 2.5f : 1.0f), 6.0f);
                }
            }
        } else if (ok && model.kind == Loaded::Kind::Creature) {
            rig = model.rig->joints;
            jointCount = model.rig->count;
            for (int k = 0; k < jointCount; ++k) j[k] = lerpMat(enemy.prevJoints[k], enemy.joints[k], alpha);
            Mat4 skin[kMaxJoints];
            skinMatrices(model.body, j, skin);
            renderer.skinned(model.body.mesh, skin, model.body.jointCount, 0, rgba(30, 26, 30));
        } else if (ok) {
            const EnemyDef& d = enemy.def();
            rig = d.rig;
            jointCount = d.jointCount;
            for (int k = 0; k < d.jointCount; ++k) j[k] = lerpMat(enemy.prevJoints[k], enemy.joints[k], alpha);
            float flash = enemy.state == AiState::Windup && std::fmod(enemy.stateTime, 0.16f) < 0.06f ? 0.35f : 0.0f;
            if (enemy.state == AiState::Dying) flash = 0.4f + 0.6f * smoothstep01(enemy.stateTime / 0.3f);
            Mat4 skin[kMaxJoints];
            skinMatrices(model.body, j, skin);
            renderer.skinned(model.body.mesh, skin, model.body.jointCount, flash, rgba(20, 20, 30));
        }

        RenderCamera rc;
        Vec3 target = focus + Vec3{0, cam.height, 0};
        Vec3 dir{std::cos(cam.pitch) * std::sin(cam.yaw), -std::sin(cam.pitch), std::cos(cam.pitch) * std::cos(cam.yaw)};
        Vec3 eye = target - dir * cam.distance;
        rc.aspect = float(w) / float(std::max(h, 1));
        rc.fovY = 0.75f;
        rc.view = lookAt(eye, target, {0, 1, 0});
        rc.proj = perspective(rc.fovY, rc.aspect, rc.zNear, rc.zFar);
        rc.position = eye;
        rc.forward = normalize(target - eye);
        rc.right = normalize(cross(rc.forward, {0, 1, 0}));
        rc.up = cross(rc.right, rc.forward);

        glViewport(0, 0, w, h);
        renderer.beginFrame(w, h);
        renderer.render(rc, focus, renderer.sceneWidth(), renderer.sceneHeight(), simTime);
        if (skeleton && ok && model.still()) {  // cajas de colisión y luces de las piezas
            glDisable(GL_DEPTH_TEST);
            const PropLibrary& lib = propLibrary();
            for (size_t k = 0; k < model.props.size(); ++k) {
                const PropDef& d = lib[size_t(model.props[k])];
                Mat4 at = translate(model.spots[k]);
                for (const AABB& b : d.colliders) lines.box(at, b.center(), b.halfExtents(), rgba(255, 90, 60));
                for (const PropLight& l : d.lights) {
                    Vec3 p = model.spots[k] + l.pos;
                    for (Vec3 ax : {Vec3{0.25f, 0, 0}, Vec3{0, 0.25f, 0}, Vec3{0, 0, 0.25f}}) lines.line(p - ax, p + ax, rgba(255, 230, 120));
                }
            }
            lines.flush(rc.proj * rc.view, 7.0f);
        } else if (skeleton && ok) {  // los huesos, por encima de la malla
            glDisable(GL_DEPTH_TEST);
            for (int k = 0; k < jointCount; ++k) {
                Vec3 p = translationOf(j[k]);
                if (rig[k].parent >= 0) lines.line(translationOf(j[rig[k].parent]), p, rgba(80, 230, 200));
                lines.line(p, p + Vec3{0, 0.02f, 0}, rgba(255, 240, 150));
            }
            lines.flush(rc.proj * rc.view, 7.0f);
        }
        renderer.endFrame();

        // Panel.
        const float uh = 720, uw = std::floor(float(w) / float(h) * uh + 0.5f);
        glEnable(GL_BLEND);
        glDisable(GL_DEPTH_TEST);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        TextStyle title{FontFace::Body, Align::Left, rgba(0, 0, 0, 200), 0};
        TextStyle body{FontFace::Body, Align::Left, rgba(0, 0, 0, 170), 0};
        const uint32_t white = rgba(240, 236, 226), soft = rgba(196, 192, 186), gold = rgba(255, 214, 130);
        ui.shapes.quad({0, 0, 0}, {380, 0, 0}, {380, uh, 0}, {0, uh, 0}, rgba(10, 10, 14, 150));
        float y = 34;
        drawText(ui, 20, y, 30, model.kind == Loaded::Kind::Gallery ? (stemOf(path) + "/").c_str() : (stemOf(path) + ".glb").c_str(),
                 white, title);
        y += 30;
        if (model.kind != Loaded::Kind::None) {
            drawText(ui, 20, y, 17, model.what.c_str(), soft, body);
            y += 22;
            char stats[128];
            if (model.kind == Loaded::Kind::Gallery)
                std::snprintf(stats, sizeof(stats), "%zu piezas · %zu triángulos · %d luces", model.props.size(), model.triangles,
                              model.lights);
            else if (model.still())
                std::snprintf(stats, sizeof(stats), "%zu triángulos · %d colisiones · %d luces", model.triangles, model.colliders,
                              model.lights);
            else
                std::snprintf(stats, sizeof(stats), "%zu triángulos · %d huesos · %zu materiales", model.triangles, model.bones,
                              model.materials.size());
            drawText(ui, 20, y, 17, stats, soft, body);
            y += 32;
            drawText(ui, 20, y, 19, "Materiales", gold, body);
            y += 22;
            for (size_t k = 0; k < model.materials.size(); ++k) {
                const std::string& mname = model.materials[k];
                if (y > 380 && k + 1 < model.materials.size()) {
                    std::string more = "… y " + std::to_string(model.materials.size() - k) + " más";
                    drawText(ui, 30, y, 16, more.c_str(), soft, body);
                    y += 21;
                    break;
                }
                drawText(ui, 30, y, 16, mname.c_str(), soft, body);
                y += 21;
            }
            y += 14;
            if (animCount > 0) drawText(ui, 20, y, 19, "Animación", gold, body);
            y += 22;
            for (int k = 0; k < animCount; ++k) {
                char a[64];
                std::snprintf(a, sizeof(a), "%d  %s", k + 1,
                              model.kind == Loaded::Kind::Creature ? kCreatureAnims[k] : model.humanoid ? kHumanAnims[k] : kEnemyAnims[k]);
                drawText(ui, 30, y, 17, a, k == anim ? gold : soft, body);
                y += 23;
            }
            if (paused) drawText(ui, 30, y + 6, 15, "(en pausa)", white, body);
        }
        if (!error.empty()) {  // el error, partido en líneas
            float ey = uh - 150;
            size_t from = 0;
            while (from < error.size() && ey < uh - 70) {
                size_t nl = error.find('\n', from);
                std::string line = error.substr(from, nl == std::string::npos ? std::string::npos : nl - from);
                drawText(ui, 400, ey, 16, line.c_str(), rgba(255, 120, 110), body);
                ey += 22;
                if (nl == std::string::npos) break;
                from = nl + 1;
            }
        } else if (now - statusTime < 3.0) {
            drawText(ui, 400, uh - 70, 16, status.c_str(), rgba(150, 230, 160), body);
        }
        drawText(ui, 20, uh - 70, 15, "Ratón: girar · rueda: acercar · botón derecho: subir", soft, body);
        drawText(ui, 20, uh - 48, 15,
                 model.kind == Loaded::Kind::Gallery ? "WASD: moverse · F: colisiones y luces"
                 : model.still()                     ? "F: colisiones y luces"
                                                     : "1-7 / Tab: animación · Espacio: pausa · F: huesos",
                 soft, body);
        drawText(ui, 20, uh - 26, 15, "V: noche · C: centrar · R: recargar · Esc: salir", soft, body);
        if (ok && model.kind == Loaded::Kind::Gallery) {  // el nombre de cada pieza, encima
            Mat4 vp = rc.proj * rc.view;
            TextStyle tag{FontFace::Body, Align::Center, rgba(0, 0, 0, 220), 0};
            const PropLibrary& lib = propLibrary();
            for (size_t k = 0; k < model.props.size(); ++k) {
                const PropDef& d = lib[size_t(model.props[k])];
                Vec3 top = model.spots[k] + Vec3{d.bounds.center().x, d.bounds.max.y + 0.4f, d.bounds.center().z};
                const float* vm = vp.m;
                float cx = vm[0] * top.x + vm[4] * top.y + vm[8] * top.z + vm[12], cy = vm[1] * top.x + vm[5] * top.y + vm[9] * top.z + vm[13];
                float cw = vm[3] * top.x + vm[7] * top.y + vm[11] * top.z + vm[15];
                if (cw < 0.1f || length(top - rc.position) > 60.0f) continue;
                float sx = (cx / cw * 0.5f + 0.5f) * uw, sy = (1.0f - (cy / cw * 0.5f + 0.5f)) * uh;
                if (sx < 390 || sx > uw || sy < 0 || sy > uh) continue;
                drawText(ui, sx, sy, 14, d.name.c_str(), white, tag);
            }
        }
        ui.flush(ortho(0, uw, uh, 0, -1, 1));
        glDisable(GL_BLEND);

        if (shotPath && now - start >= shotTime) {
            if (shotCount == 1) {
                savePpm(shotPath, w, h);
            } else {
                char name[512];
                std::snprintf(name, sizeof(name), "%s_%03d.ppm", shotPath, shotIndex);
                savePpm(name, w, h);
            }
            quit = ++shotIndex >= shotCount;
        }
        platform::swapBuffers();
        if (!platform::vsyncEnabled()) platform::sleepSeconds(kStep - (platform::timeSeconds() - now));
    }
    return ok ? 0 : 1;
}
