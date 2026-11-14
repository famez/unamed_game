#pragma once

// Estado y reglas del juego: escenario, protagonista, enemigos por oleadas, combate,
// magias, botín, cofres e inventario. game.cpp contiene la lógica; game_draw.cpp el
// dibujado (mundo 3D, efectos y HUD).

#include "character.h"
#include "debug_draw.h"
#include "enemy.h"
#include "items.h"
#include "physics.h"
#include "renderer.h"
#include "sfx.h"
#include "story.h"
#include "skeleton.h"
#include "text.h"
#include "vmath.h"

#include <cstdint>
#include <deque>
#include <map>
#include <set>
#include <string>
#include <vector>

// Hoja del arma en espacio local de la muñeca derecha: sale perpendicular al antebrazo
// (+Z local), desde la empuñadura hasta la punta (la longitud la da el arma).
constexpr Vec3 kGripLocal{0, -0.08f, 0};
constexpr Vec3 kBladeBase{0, -0.08f, 0.10f};
constexpr float kTrailLife = 0.16f;  // segundos que dura la estela del arma

struct DrawFlags {
    bool bones = true;
    bool joints = true;
    bool body = true;
    bool grid = true;
};

// Órdenes del jugador para un paso de simulación. Los flancos (saltar, atacar...) valen
// true en un único paso.
struct Controls {
    Vec3 move;               // dirección deseada en el mundo (XZ), longitud 0..1
    bool jump = false;
    bool attack = false;
    bool dodge = false;
    bool guard = false;      // mantenido
    bool interact = false;   // abrir cofres
    bool lockOn = false;     // fijar objetivo / pasar al siguiente
    bool unlock = false;     // soltar el objetivo
    int command = -1;        // 0..5: ejecutar directamente esa entrada del menú de comandos
    bool runCommand = false; // ejecutar la entrada seleccionada
    int commandScroll = 0;   // mover la selección del menú de comandos
    Vec3 camForward{0, 0, 1};  // hacia dónde mira la cámara (para elegir objetivo)
    bool advance = false;      // pasar la línea de diálogo
};

// Datos de la cámara para dibujar: billboards, proyección de etiquetas al HUD.
struct ViewInfo {
    Mat4 viewProj = Mat4::identity();
    Vec3 eye, right{1, 0, 0}, up{0, 1, 0};
    int width = 1, height = 1;
};

enum class Spell { Fire, Ice, Cure, Count };

struct SpellDef {
    const char* name;
    float cost;  // PM
    uint32_t color;
};
const SpellDef& spellDef(Spell s);

enum class ProjectileKind { Fire, Ice, EnemyOrb };

struct Projectile {
    ProjectileKind kind;
    Vec3 pos, vel;
    float life = 0;       // segundos que le quedan
    int target = -1;      // enemigo al que persigue (magias), -1 = ninguno
    bool reflected = false;  // orbe enemigo devuelto de un golpe: ahora daña a los enemigos
    bool dead = false;
    float seed = 0;
};

enum class PickupKind { Hp, Mp, Munny, Item };

// Botín: salta del enemigo, rebota y, al acercarse el jugador, vuela hacia él.
struct Pickup {
    PickupKind kind;
    ItemId item = ItemId::Potion;
    int value = 0;
    Vec3 pos, vel;
    float age = 0, seed = 0;
    bool resting = false;
    bool dead = false;
};

struct Particle {
    Vec3 pos, vel;
    float life, maxLife;
    float size;
    uint32_t color;
    float gravity = 0;  // m/s² hacia abajo (negativo = asciende)
    bool star = false;  // destello de 4 puntas (si no, punto suave)
};

struct Ring {  // onda expansiva en el suelo
    Vec3 pos;
    float age, life, radius;
    uint32_t color;
};

struct Blast {  // bola de fuego de la magia Fuego
    Vec3 pos;
    float age, radius, seed;
};

struct Notice {  // "OBTENIDO: POCION" a la derecha de la pantalla
    std::string text;
    uint32_t color;
    float age = 0;
};

struct TreasureChest {
    Vec3 pos;
    float yaw = 0;
    std::vector<std::pair<ItemId, int>> contents;
    int munny = 0;
    bool open = false;
    float openTime = 0;
};

struct Crate {  // caja rompible: suelta dinero y orbes
    AABB box;
    float hp = 2;
    bool broken = false;
    float hitFlash = 0;
};

// Salpicadura de color que deja un Gris al disiparse (devuelve el color a su alrededor).
struct ColorSplash {
    Vec3 pos;
    float radius;
    uint32_t color;
    float age = 0;
};

struct TrailSample {  // estela del arma: base y punta de la hoja en un instante
    Vec3 base, tip;
    float age;
};

// --- Escenarios -----------------------------------------------------------------------
// Cada escenario se divide en partes con un objetivo (vencer un combate, encender nodos,
// encender faroles, usar algo, llegar a un sitio, salir). Al cumplirlo se abren puertas,
// suena su escena y empieza la siguiente.

enum class LevelId { Plaza, Mercado, Puente, Count };

struct Spawn {
    EnemyKind kind;
    Vec3 pos;
    bool boss = false;
    const char* name = nullptr;  // jefes: su nombre propio
};

// Combate de zona: al entrar en `trigger` aparecen los Grises por rondas y se cierran las
// barreras hasta vencerlos. `armed` = false hasta que el guion lo arme (jefes).
struct Encounter {
    std::string id;
    AABB trigger;
    std::vector<std::vector<Spawn>> rounds;
    std::vector<std::string> barriers;
    std::string sceneStart, sceneClear;
    bool armed = true;
    int round = -1;
    bool started = false, cleared = false;
};

// Barrera de energía o puerta: sólida mientras está cerrada.
struct Gate {
    std::string id;
    AABB box;
    bool closed = false;
    uint32_t color = 0;
    float openTime = 0;  // para el desvanecido al abrirse
};

// Nodo cromático: repartía la luz del Faro por el barrio. Encenderlo devuelve el color a
// su alrededor (zona de color en el render). Solo se puede si no hay Grises cerca.
struct ColorNode {
    std::string id;
    Vec3 pos;
    float radius = 11;
    uint32_t color = 0;
    std::string scene;
    bool lit = false;
    float litTime = 0;
};

// Farol del puente: punto de control.
struct Lantern {
    Vec3 pos;
    bool lit = false;
    float litTime = 0;
};

struct Interactable {
    std::string id;
    Vec3 pos;
    std::string label;  // "Examinar el puesto"...
    std::string scene;
    bool used = false;
};

enum class PartKind { Encounter, Nodes, Lanterns, Interact, Reach, Exit };

struct LevelPart {
    std::string title, objective;
    PartKind kind;
    std::string target;  // id del combate o de la interacción
    int count = 0;       // nodos o faroles necesarios
    AABB area;           // Reach / Exit
    Vec3 marker;         // hacia dónde señala el indicador del objetivo
    std::vector<std::string> openGates;
    std::string sceneStart, sceneDone;
};

// Eco de memoria: fragmento de luz que, al tocarlo, reproduce un recuerdo del guion.
struct MemoryEcho {
    Vec3 pos;
    std::string scene;
    bool active = false, taken = false;
    float appear = 0;  // segundos desde que apareció
};

// Bloque sólido del escenario con su aspecto.
struct Block {
    AABB box;
    uint32_t color;
    Material material;
};

// Decoración sin colisión (copas de árboles, flores, tejados...): se calcula una vez.
struct Decor {
    Mat4 transform;
    Vec3 center, half;
    uint32_t color;
    Material material;
    bool sphere;
};

class Game {
public:
    static constexpr int kCommandCount = 3 + Inventory::kQuickSlots;  // magias + atajos de objetos

    // `assetDir`: carpeta assets (para cargar el guion). Sin ella no hay historia: el
    // juego empieza directamente con las oleadas (lo usan los tests).
    void init(const char* assetDir = nullptr);
    void loadLevel(LevelId id);
    void update(float dt, const Controls& c);

    // Con `solid` (render con shaders) los cuerpos van ahí, `glow` recibe los efectos
    // aditivos (estelas, destellos, halos) y `lines` lo poco que se dibuja con líneas. Con
    // nullptr, todo en alambre (modo debug: esqueletos, cajas, sin casi trabajo de GPU).
    // `alpha` ∈ [0,1]: punto entre el paso de simulación anterior y el actual (interpolación).
    void draw(DebugDraw& lines, DebugDraw& glow, Renderer* solid, const DrawFlags& flags, const ViewInfo& view,
              float alpha = 1) const;
    void drawHud(UiLayer& ui, const ViewInfo& view, bool debug) const;

    const Character& hero() const { return hero_; }
    float hp() const { return hp_; }
    float maxHp() const { return maxHp_; }
    float mp() const { return mp_; }
    float maxMp() const { return maxMp_; }
    Inventory& inventory() { return inv_; }
    const Inventory& inventory() const { return inv_; }
    // Usar un consumible (desde el inventario o un atajo). Falso si no hay o no hace falta.
    bool useItem(ItemId id);
    bool equip(ItemId id);
    float time() const { return time_; }
    int kills() const { return kills_; }
    size_t enemyCount() const { return enemies_.size(); }
    size_t pickupCount() const { return pickups_.size(); }
    // Posición del objetivo fijado (para la cámara). Falso si no hay.
    bool lockTarget(Vec3& pos) const;
    float cameraShake() const { return shake_; }
    // Escenario y objetivo actuales.
    LevelId level() const { return level_; }
    const std::string& levelName() const { return levelName_; }
    int partIndex() const { return int(part_); }
    int partCount() const { return int(parts_.size()); }
    const LevelPart* currentPart() const { return part_ < parts_.size() ? &parts_[part_] : nullptr; }
    float transition() const { return transition_; }
    // Historia.
    const Dialogue& dialogue() const { return dialogue_; }
    bool dialogueBlocking() const { return dialogue_.blocking(); }
    std::string speakerName(const std::string& id) const;
    // Sonidos emitidos desde la última llamada (los reproduce main con el audio).
    std::vector<SoundEvent> takeSounds() {
        std::vector<SoundEvent> out;
        out.swap(sounds_);
        return out;
    }
    bool inCombat() const;
    // "El color es energía": 0 = ciudad gris y apagada, 1 = a todo color.
    float croma() const { return croma_; }
    // Apertura extra del campo de visión (radianes) en acercamientos y remates.
    float fovKick() const { return fovKick_; }
    Vec3 heroRenderPos(float alpha) const { return hero_.interpolatedPos(alpha); }
    // Fracción [0,1] del segmento from→to libre de escenario (para que la cámara no
    // atraviese las casas).
    float cameraClear(Vec3 from, Vec3 to) const;

private:
    void buildPlaza();
    void buildMercado();
    void buildPuente();
    void addBlock(Vec3 c, Vec3 h, uint32_t col, Material m = Material::Stone);
    void addDecor(const Mat4& t, Vec3 c, Vec3 h, uint32_t col, Material m, bool sphere);
    void addRim(Vec3 c, float hx, float hz, uint32_t col);
    void addBuilding(float cx, float cz, float hx, float hz, float height, Vec3 in, uint32_t tint, uint32_t neon);
    void addTree(Vec3 base, float size, uint32_t leaves);
    void addGate(const std::string& id, Vec3 c, Vec3 h, uint32_t color, bool closed);
    void updateLevel(float dt);
    bool partDone(const LevelPart& p) const;
    void setGate(const std::string& id, bool closed);
    void spawnEnemy(const Spawn& s);
    void resetEncounter();
    // Interacción con lo más cercano (cápsula, nodo, farol, objeto). -1 si no hay nada.
    struct InteractTarget {
        int kind = -1;  // 0 cápsula, 1 nodo, 2 farol, 3 objeto
        int index = -1;
        std::string label;
    };
    InteractTarget nearbyInteract() const;
    void tryInteract();
    void rebuildSolids();
    void applyWeapon();
    void cycleLock(Vec3 camForward);
    int pickTarget(Vec3 dir, float range) const;
    bool heroAttack(Vec3 moveInput);
    void runCommand(int index);
    bool castSpell(Spell s);
    void resolveImpact(Move m);
    void hitEnemy(Enemy& e, Vec3 dir, float damage, float knock, float lift, bool heavy);
    void killEnemy(Enemy& e);
    void hurtHero(Vec3 dir, float damage, float knock, Enemy* source);
    void updateEnemy(Enemy& e, float dt);
    void enemyStartAttack(Enemy& e);
    void updateProjectiles(float dt);
    void explodeFire(Vec3 point);
    void updatePickups(float dt);
    void collect(Pickup& p);
    void dropLoot(Vec3 pos, int munny, int hpOrbs, int mpOrbs, ItemId item, bool withItem);
    void updateEffects(float dt);
    void breakCrate(Crate& c);
    void notify(const std::string& text, uint32_t color);
    void heal(float hp, float mp);
    void castImpact(Spell s);
    void spawnSparks(Vec3 pos, Vec3 dir, int count, uint32_t color, float speed, bool stars = true);
    void spawnRing(Vec3 pos, float radius, float life, uint32_t color);
    float groundHeight(Vec3 p) const;
    Vec3 bladeTip() const;
    float random01();
    float randRange(float a, float b) { return a + (b - a) * random01(); }

    void playScene(const std::string& id);
    void storyCommand(const StoryLine& cmd);
    void updateStory(float dt);
    void updateBrizna(float dt);
    void sound(Sfx id, Vec3 pos, float gain = 1, float pitch = 1);
    void uiSound(Sfx id, float gain = 1, float pitch = 1);
    void defeatHero();
    void respawnHero();

    // Dibujado (game_draw.cpp).
    void drawLevel(DebugDraw& lines, Renderer* solid, const DrawFlags& f, const ViewInfo& view, float alpha) const;
    void drawHero(DebugDraw& lines, Renderer* solid, const DrawFlags& f, float alpha) const;
    void drawEnemies(DebugDraw& lines, DebugDraw& glow, Renderer* solid, const DrawFlags& f, const ViewInfo& view,
                     float alpha) const;
    void drawEffects(DebugDraw& lines, DebugDraw& glow, Renderer* solid, const ViewInfo& view) const;
    void drawStory(DebugDraw& lines, DebugDraw& glow, Renderer* solid, const ViewInfo& view) const;
    void drawStoryHud(UiLayer& ui, const ViewInfo& view) const;

    World world_;
    std::vector<Block> blocks_;  // escenario fijo (world_ = esto + cajas + cofres)
    std::vector<Decor> decor_;
    std::vector<Vec3> lamps_;
    Vec3 heroStart_;
    float heroStartYaw_ = 0;
    LevelId level_ = LevelId::Plaza;
    std::string levelName_;
    std::vector<LevelPart> parts_;
    size_t part_ = 0;
    float partBanner_ = 0;
    std::vector<Gate> gates_;
    std::vector<ColorNode> nodes_;
    std::vector<Lantern> lanterns_;
    std::vector<Interactable> interacts_;
    std::vector<Encounter> encounters_;
    int activeEncounter_ = -1;
    Vec3 checkpoint_;
    float checkpointYaw_ = 0;
    LevelId nextLevel_ = LevelId::Plaza;
    bool lastLevel_ = false;
    float transition_ = 0;     // fundido a negro al cambiar de escenario (0..1)
    bool leaving_ = false;
    uint32_t floorColor_ = 0;
    Material floorMaterial_ = Material::Ground;
    Character hero_;
    Inventory inv_;
    float hp_ = 100, maxHp_ = 100;
    float mp_ = 50, maxMp_ = 50;
    float hpGhost_ = 100;     // barra roja que va detrás al perder vida
    float heroInvuln_ = 0;    // tras un golpe, un momento sin recibir otro
    float heroFlash_ = 0;
    float guardFlash_ = 0;    // destello de bloqueo (perfecto = más fuerte)
    bool perfectGuard_ = false;
    Spell pendingSpell_ = Spell::Fire;  // la que sale al llegar el impacto del Cast
    int commandSel_ = 0;
    int lock_ = -1;           // enemigo fijado
    std::vector<Enemy> enemies_;
    std::vector<Projectile> projectiles_;
    std::vector<Pickup> pickups_;
    std::vector<Particle> particles_;
    std::vector<Ring> rings_;
    std::vector<Blast> blasts_;
    std::vector<Notice> notices_;
    std::vector<TreasureChest> chests_;
    std::vector<Crate> crates_;
    std::vector<TrailSample> trail_;
    // Historia: guion, escena en curso, cola de escenas y lo ya visto.
    Script script_;
    bool story_ = false;
    Dialogue dialogue_;
    std::deque<std::string> sceneQueue_;
    std::set<std::string> played_;
    std::map<std::string, std::string> names_;
    float fade_ = 0;              // 1 = pantalla en negro (despertar)
    float chapterBanner_ = 0;     // cartel de capítulo
    bool chapterEnded_ = false;
    bool standingUp_ = false;
    bool seenShroom_ = false, seenMoth_ = false;
    std::vector<MemoryEcho> echoes_;
    Vec3 briznaPos_, briznaVel_;
    std::vector<SoundEvent> sounds_;
    float croma_ = 0.12f;        // sube al disipar Grises y al superar oleadas
    float cromaShown_ = 0.12f;   // el que se ve: lo sigue suavemente
    std::vector<ColorSplash> splashes_;
    int kills_ = 0;
    float hitStop_ = 0;       // congelación breve al golpear (da peso a los golpes)
    float shake_ = 0;         // temblor de cámara
    float fovKick_ = 0;
    // Pulsaciones guardadas un instante: si llegan un poco antes de poder ejecutarse
    // (en mitad de una voltereta, por ejemplo) no se pierden.
    float attackBuffer_ = 0, dodgeBuffer_ = 0;
    Vec3 bufferedMove_;
    // Último golpe (para dibujarlo en modo debug): sector delante del jugador.
    Vec3 lastHitOrigin_, lastHitDir_;
    float lastHitRange_ = 0, lastHitCos_ = 1, lastHitAge_ = 10;
    float time_ = 0;
    uint32_t rng_ = 12345;
};
