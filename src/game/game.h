#pragma once

// Estado y reglas del juego: escenario, protagonista, enemigos por oleadas, combate,
// magias, botín, cofres e inventario. game.cpp contiene la lógica; game_draw.cpp el
// dibujado (mundo 3D, efectos y HUD).

#include "actors/brain.h"
#include "actors/character.h"
#include "render/debug_draw.h"
#include "actors/enemy.h"
#include "game/items.h"
#include "actors/physics.h"
#include "render/renderer.h"
#include "audio/sfx.h"
#include "game/story.h"
#include "actors/skeleton.h"
#include "render/text.h"
#include "core/vmath.h"

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
    bool lockOn = false;     // R: fijar / soltar objetivo (dentro de una fuente: elegir fuente)
    bool nextTarget = false; // clic central: pasar al siguiente objetivo
    bool unlock = false;     // soltar el objetivo
    int command = -1;        // 0..5: ejecutar directamente esa entrada del menú de comandos
    bool runCommand = false; // ejecutar la entrada seleccionada
    int commandScroll = 0;   // mover la selección del menú de comandos
    Vec3 camForward{0, 0, 1};  // hacia dónde mira la cámara (para elegir objetivo)
    bool advance = false;      // pasar la línea de diálogo
    Vec3 aimOrigin, aimDir{0, 0, 1};  // rayo de la cámara (para apuntar a fuentes de luz)
    bool sprint = false;       // mantener: correr
    bool light = false;        // Q: entrar en la fuente de luz de al lado / saltar a otra / fundirse en la sombra
    bool transform = false;    // G: transformarse (la forma de la mitad activa, 50 de Poder)
    bool summon = false;       // T: invocar un compañero
    bool swapMode = false;     // Z: cambiar entre luz y sombra
    bool attackHeld = false;   // ataque mantenido (corte cargado)
};

// Datos de la cámara para dibujar: billboards, proyección de etiquetas al HUD.
struct ViewInfo {
    Mat4 viewProj = Mat4::identity();
    Vec3 eye, right{1, 0, 0}, up{0, 1, 0};
    int width = 1, height = 1;
};

// Técnicas, cinco por mitad, en el menú de comandos (1-5, o rueda + E). Luz: lanza que
// persigue, tres esquirlas que congelan, curación, una bola que estalla y la Supernova (con
// el Poder lleno). Sombra: púas que recorren el suelo y lanzan por los aires, cadenas que
// inmovilizan alrededor, drenar vida, una mina que implosiona y el agujero negro.
enum class Spell { Fire, Ice, Cure, Burst, Nova, Spikes, Chains, Drain, DarkBomb, Hole, Count };

struct SpellDef {
    const char* name;
    float cost;  // Poder (la Supernova pide la barra llena y la gasta entera)
    uint32_t color;
};
const SpellDef& spellDef(Spell s);
constexpr int kSpellSlots = 5;
Spell modeSpell(bool shadow, int slot);

enum class ProjectileKind { Fire, Ice, EnemyOrb, Wave, Burst, Spikes };

struct Projectile {
    ProjectileKind kind;
    Vec3 pos, vel;
    float life = 0;       // segundos que le quedan
    int target = -1;      // enemigo al que persigue (magias), -1 = ninguno
    bool reflected = false;  // orbe enemigo devuelto de un golpe: ahora daña a los enemigos
    bool dead = false;
    float seed = 0;
    uint32_t hitMask = 0;  // onda: Grises ya alcanzados (por índice, los 32 primeros)
};

// Mp = Poder (la única barra), en orbe dorado; Shadow = Poder en orbe violeta (lo sueltan los
// Grises de sombra: mismo efecto, distinto aspecto).
enum class PickupKind { Hp, Mp, Munny, Item, Shadow, Xp };

// Botín: salta del enemigo, rebota y, al acercarse el jugador, vuela hacia él.
struct Pickup {
    PickupKind kind;
    ItemId item = ItemId::Potion;
    int value = 0;
    Vec3 pos, vel;
    float age = 0, seed = 0;
    bool resting = false;
    bool dead = false;
    bool placed = false;  // puesto en el mapa (no lo ha soltado nadie): no caduca
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

// Efectos de golpe. Cada familia se puede activar por separado (--efectos-golpe):
//   tajo      el trazo del arma, una media luna de luz que cruza al enemigo en el sentido del golpe
//   destello  el instante del impacto: estrella de rayos largos y un fogonazo que ilumina la escena
//   materia   lo que suelta el enemigo: esquirlas de cristal (los de luz) o gotas de tinta que
//             manchan el suelo (los de sombra)
//   sacudida  el enemigo se dobla hacia donde le empujan, rebota y se aplasta un instante
//   camara    la cámara da un tirón en la dirección del golpe y vuelve
//   remate    en los remates: líneas de impacto alrededor del golpe y una onda de choque vertical
enum HitFx : unsigned {
    kFxSlash = 1, kFxFlash = 2, kFxMatter = 4, kFxRecoil = 8, kFxCamera = 16, kFxFinisher = 32, kFxAll = 63
};
// Traduce "tajo,destello,..." (o "todos" / "ninguno") a la máscara; false si no entiende algo.
bool parseHitFx(const char* list, unsigned& mask);

struct SlashMark {  // tajo: media luna que se dibuja de una punta a otra y se desvanece
    Vec3 pos, along, bend;  // centro, sentido del golpe y hacia dónde se curva
    float age, life, size;
    uint32_t color;
};

struct ImpactFlash {  // destello del impacto (y líneas de impacto en los remates)
    Vec3 pos, dir;
    float age, life, size, angle;
    uint32_t color;
    bool finisher;
};

struct Debris {  // esquirla de cristal o gota de tinta
    Vec3 pos, vel;
    float age, life, size, spin, ground;
    uint32_t color;
    bool ink;
};

struct InkSplat {  // mancha de tinta en el suelo
    Vec3 pos;
    float age, life, radius, angle, stretch;
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

// Transformaciones (G, 50 de Poder, 20 s) y compañeros invocables (allies.cpp). Cada mitad
// tiene la suya: la luz, la Forma Solar (pelo de fuego dorado, aura y rayos; todo más rápido
// y más fuerte, los remates lanzan ondas); la sombra, el Lobo de sombra (una bestia enorme
// que corre y muerde, y cada mordisco le devuelve vida).
enum class Form { None, Solar, Wolf };
const char* formName(Form f);
uint32_t formColor(Form f);

struct Companion {
    int kind = 0;  // 0 chispa aliada, 1 eco de Nora
    Vec3 pos, vel;
    float life = 0, timer = 0;
};

// --- Escenarios -----------------------------------------------------------------------
// Cada escenario se divide en partes con un objetivo (vencer un combate, encender nodos,
// encender faroles, usar algo, llegar a un sitio, salir). Al cumplirlo se abren puertas,
// suena su escena y empieza la siguiente.

// Campo, Neon, Cafetero y Rio: mapas grandes (terreno con relieve y piezas .glb de assets/props).
enum class LevelId { Plaza, Mercado, Puente, Prueba, Selva, Cueva, Callejones, Campo, Neon, Cafetero, Rio, Copas, Count };

// Modos de prueba (sin historia, con vida, Luz y Sombra infinitas): un escenario aleatorio
// con Grises sin fin, un archipiélago de islas flotantes para practicar el viaje por la luz,
// o un patio con charcos de oscuridad para practicar la sombra.
enum class TestMode { None, Combat };

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

// Rompecabezas de luz: un emisor lanza un haz; los espejos (F: girar 45°) lo desvían y
// los que llevan un cristal de color lo tiñen; un receptor iluminado (del color pedido)
// abre su puerta.
struct LightEmitter {
    Vec3 pos;
    Vec3 dir;
    uint32_t color = 0;
};
struct Mirror {
    Vec3 pos;         // base del pedestal
    int angle = 0;    // orientación de la normal: angle × 45°
    uint32_t filter = 0;  // tiñe el haz (0 = espejo normal)
};
struct Receptor {
    std::string id;
    Vec3 pos;
    uint32_t needColor = 0;  // 0 = cualquier luz
    std::string gate, scene;
    bool lit = false;
    float litTime = 0;
};

enum class PartKind { Encounter, Nodes, Lanterns, Interact, Reach, Exit, Puzzle };

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

// Forma de una fuente de luz: farola de la ciudad, antorcha (selva), cristal (cueva), farolillo
// de papel colgado (callejones) o letrero de neón en una pared.
enum class LampStyle { Post, Torch, Crystal, Paper, Neon, Prop };  // Prop: la luz de una pieza .glb (se dibuja ella)

// Farola (o fuente de luz fija): el Prismero puede entrar en ella y saltar a otras. Se puede
// romper a golpes (ya no vuelve a alumbrar) o apagar y encender con su interruptor.
struct Lamp {
    Vec3 pos;            // la bombilla
    uint32_t color = 0;
    bool bulb = true;    // dibujar la bombilla (el prisma de la fuente se dibuja aparte)
    bool on = true;      // apagada: no se puede usar
    float energy = 1;    // luz que le queda: se gasta al usarla; a 0 se apaga
    bool broken = false; // rota a golpes: apagada para siempre
    float hp = 3;        // golpes que aguanta
    float hitFlash = 0;
    LampStyle style = LampStyle::Post;
    float reach = 1;     // multiplica el radio que alumbra
};
float lampRadius(const Lamp& l);  // lo que alumbra (según su energía)

// Interruptor de un grupo de farolas: apagarlas crea oscuridad para la sombra; encenderlas
// las devuelve con toda su energía (las rotas no). Suelen estar apartados.
struct LampSwitch {
    Vec3 pos;
    std::vector<int> lamps;
    bool on = true;
    float flip = 0;  // segundos desde que se accionó (para la palanca)
};

// Portal: cruzarlo cambia de luz a sombra o al revés.
struct Portal {
    Vec3 pos;
    float spin = 0;
    bool inside = false;
};

// Agujero negro (técnica de la sombra): atrae a los enemigos y los proyectiles y acaba en
// implosión. La mina oscura es uno pequeño y rápido que estalla con más fuerza.
struct BlackHole {
    Vec3 pos;
    float life = 0, age = 0;
    bool bomb = false;
};

// Poderes: la historia los va devolviendo (sin guion, todos desde el principio).
struct Powers {
    bool strong = true;       // sin fuerzas: anda despacio
    bool attack = true, jump = true, dodge = true, sprint = true;
    bool light = true, techniques = true, nova = true;
};

// Plano de una escena: la cámara mira a `focus` desde `eye` durante `duration` segundos.
struct Shot {
    Vec3 focus, eye;
    float duration = 3;
};

// Eco: silueta de luz que repite un recuerdo (o Ilse, oscura). Sigue una lista de acciones.
struct GhostAct {
    enum Kind { Walk, Wait, Gesture, Face, Vanish } kind;
    Vec3 p;      // Walk: destino; Face: hacia dónde mira
    float t = 0; // Wait: segundos
    GhostAct(Kind k, Vec3 point = {}, float secs = 0) : kind(k), p(point), t(secs) {}
};
struct Ghost {
    Character body;
    std::vector<GhostAct> acts;
    size_t act = 0;
    float t = 0, age = 0, fade = 0;  // fade > 0: desvaneciéndose
    bool ilse = false;
    bool loop = false;  // repite sus acciones (un recuerdo atascado)
};

// Tutorial mínimo: una tecla y una palabra, hasta que se hace la acción.
struct Tutorial {
    std::string key, action, word;
    float age = 0, doneTime = -1;
};

// Bloque sólido del escenario con su aspecto.
struct Block {
    AABB box;
    uint32_t color;  // 0: invisible (solo choca; lo que se ve es decoración)
    Material material;
};

// Decoración sin colisión (copas de árboles, flores, tejados...): se calcula una vez.
// Papel de un habitante: vecino (dice su frase), la fonda de la Señora Mecha (cura y guarda),
// la tienda de la Doctora Umbra (abre el taller), la entrenadora (Brasa, enseña combos) o el
// que sigue a Vela por las zonas seguras (Pabilo).
enum class NpcRole { Townsfolk, Inn, Shop, Trainer, Follower };

// Habitante: pasea por su zona (en torno a `home`), se para de vez en cuando (en su pose de
// reposo) y mira a la protagonista si se le acerca. `model`: su .glb de assets/models/habitantes.
// Con nombre, F le habla: dice sus frases por turnos.
struct Npc {
    std::string model, name;
    NpcRole role = NpcRole::Townsfolk;
    std::vector<std::string> lines;
    int line = 0;
    float still = 0;   // 0 andando, 1 en su pose de reposo (para mezclar)
    Character body;
    Vec3 home, target;
    float radius = 8;  // hasta dónde pasea
    float wait = 0;    // segundos parado antes de echar a andar
    float walked = 0;  // segundos hacia el destino actual (si se atasca, cambia)
    float sinceTalk = 1e9f;  // desde la última vez que se le habló
    int bark = 0;            // barks de entrada: por cuál va
    uint32_t seed = 1;
};

// Pieza .glb colocada en el escenario (game/props.h).
struct PropInstance {
    int def;
    Mat4 transform;
    Vec3 center;   // de su caja envolvente, en el mundo (para no dibujar lo que no se ve)
    float radius;
};

struct Decor {
    Mat4 transform;
    Vec3 center, half;
    uint32_t color;
    Material material;
    bool sphere;
};

class Game {
public:
    static constexpr int kCommandCount = kSpellSlots + Inventory::kQuickSlots;  // técnicas + atajos de objetos

    // `assetDir`: carpeta assets (para cargar el guion). Sin ella no hay historia: el
    // juego empieza directamente con las oleadas (lo usan los tests).
    void init(const char* assetDir = nullptr);
    void loadLevel(LevelId id);
    // Desarrollo: salta la intro y reproduce ya una escena del guion (en el escenario que toque).
    void jumpToScene(const std::string& id);
    // Un nivel suelto, sin historia: sus combates y objetivos, la sombra ya despierta y los
    // trucos (lo usa el selector de niveles).
    void initLevel(LevelId id);
    // Modo de prueba con un escenario generado a partir de `seed`.
    void initTest(TestMode mode, uint32_t seed);
    // Sin historia (modo prueba o nivel suelto): Z cambia de modo y valen los trucos F1-F2.
    bool sandbox() const { return testMode_ != TestMode::None || freePlay_; }
    TestMode testMode() const { return testMode_; }
    void setTestCheats(bool hp, bool power);
    // Para los guiones de grabación (--vitrina): colocar al protagonista, quitar o poner Grises
    // y parar los que aparecen solos en el patio.
    void devPlace(Vec3 pos, float yaw);
    void devClearEnemies();
    void devSpawn(EnemyKind kind, Vec3 pos);
    void devNoSpawns(bool on) { devNoSpawns_ = on; }
    void devAttackNow(size_t i) {  // el Gris i ataca ya (para el bloqueo perfecto)
        if (i < enemies_.size() && enemies_[i].targetable()) enemyStartAttack(enemies_[i]);
    }
    void devToughen(size_t i, float k) {
        if (i < enemies_.size()) enemies_[i].health *= k;
    }
    bool devFreeSpot(Vec3& p, float radius) const;  // busca suelo libre cerca de p
    bool infiniteHp() const { return infHp_; }
    bool infinitePower() const { return infPower_; }
    uint32_t testSeed() const { return testSeed_; }
    void update(float dt, const Controls& c);

    // Con `solid` (render con shaders) los cuerpos van ahí, `glow` recibe los efectos
    // aditivos (estelas, destellos, halos) y `lines` lo poco que se dibuja con líneas. Con
    // nullptr, todo en alambre (modo debug: esqueletos, cajas, sin casi trabajo de GPU).
    // `alpha` ∈ [0,1]: punto entre el paso de simulación anterior y el actual (interpolación).
    void draw(DebugDraw& lines, DebugDraw& glow, Renderer* solid, const DrawFlags& flags, const ViewInfo& view,
              float alpha = 1) const;
    void drawHud(UiLayer& ui, const ViewInfo& view, bool debug) const;
    // Retrato del HUD: encola el busto de la protagonista (Vela o Hollín según el modo) y da la
    // cámara que lo encuadra. Falso si no hay modelo (.glb) que retratar.
    bool drawPortrait(Renderer& r, RenderCamera& cam, float aspect) const;
    static constexpr float kPortraitInset = 86;  // centro del hexágono: px virtuales desde la esquina

    const Character& hero() const { return hero_; }
    float hp() const { return hp_; }
    // Nivel del protagonista: la experiencia la sueltan los enemigos al disiparse.
    static constexpr int kMaxLevel = 30;
    static int xpToNext(int level) { return 30 + 25 * level; }
    int heroLevel() const { return heroLevel_; }
    int xp() const { return xp_; }
    void gainXp(int amount);
    float levelPower() const { return 1.5f * float(heroLevel_ - 1); }  // daño extra por nivel
    float maxHp() const { return maxHp_; }
    float mp() const { return mp_; }
    float maxMp() const { return maxMp_; }
    Inventory& inventory() { return inv_; }
    const Inventory& inventory() const { return inv_; }
    // Usar un consumible (desde el inventario o un atajo). Falso si no hay o no hace falta.
    bool useItem(ItemId id);
    bool equip(ItemId id);
    // Fabricar en el taller (consume las piezas).
    bool craft(const Recipe& r);
    float time() const { return time_; }
    int kills() const { return kills_; }
    size_t enemyCount() const { return enemies_.size(); }
    size_t pickupCount() const { return pickups_.size(); }
    // Posición del objetivo fijado (para la cámara). Falso si no hay.
    bool lockTarget(Vec3& pos) const;
    float cameraShake() const { return shake_; }
    // Cerebros de los Grises (aprendizaje por refuerzo): se cargan y guardan en `dir`.
    void loadBrains(const std::string& dir);
    void saveBrains() const;
    const Brain& brain(int kind) const { return brains_[kind]; }
    const Brain& bossBrain() const { return bossBrain_; }
    // Intensidad de luz (0..1) en un punto: zonas de nodos encendidos y faroles.
    float lightAt(Vec3 p) const;
    float lampLightAt(Vec3 p) const;  // luz de las farolas encendidas
    // Viaje por la luz: dentro de una fuente, o la fuente señalada (al lado para entrar o, ya
    // dentro, la elegida para saltar; -1 si no hay).
    bool insideLight() const { return inLight_; }
    // Luz o sombra: el protagonista cambia de una a otra (Z) cuando ya ha despertado la sombra.
    bool shadowMode() const { return shadowMode_; }
    const std::vector<Lamp>& lamps() const { return lamps_; }
    const std::vector<LampSwitch>& lampSwitches() const { return switches_; }
    bool shadowUnlocked() const { return shadowUnlocked_; }
    void unlockShadow() { shadowUnlocked_ = true; }
    // Oscuridad (0..1) en un punto: donde no llega la luz de las farolas, nodos y faroles.
    float shadowAt(Vec3 p) const;
    bool sunBlocked(Vec3 p) const;  // de día: algo tapa el sol en ese punto
    bool diving() const { return diving_; }
    bool onWall() const { return onWall_; }
    bool riposting() const { return riposteStep_ >= 0; }
    float riposteWindow() const { return riposteWindow_; }
    const World& world() const { return world_; }
    const std::vector<Enemy>& enemies() const { return enemies_; }
    void addPortal(Vec3 pos) { portals_.push_back({pos}); }
    const std::vector<Portal>& portals() const { return portals_; }
    Vec3 hostLight() const { return hostPos_; }
    int lightTarget() const { return lightTarget_; }
    Vec3 lightTargetPos() const { return lightTarget_ >= 0 ? lightPoints_[size_t(lightTarget_)].pos : Vec3{}; }
    bool travelingByLight() const { return travel_; }
    Form form() const { return form_; }
    static constexpr float kFormDuration = 20.0f, kFormCost = 50.0f;
    float formTime() const { return formTime_; }
    // Escenario y objetivo actuales.
    LevelId level() const { return level_; }
    float viewDistance() const { return viewDistance_; }
    const std::vector<PropInstance>& placedProps() const { return props_; }
    const std::vector<Npc>& npcs() const { return npcs_; }
    bool takeShopRequest() {
        bool r = shopRequest_;
        shopRequest_ = false;
        return r;
    }
    const std::string& levelName() const { return levelName_; }
    int partIndex() const { return int(part_); }
    int partCount() const { return int(parts_.size()); }
    const LevelPart* currentPart() const { return part_ < parts_.size() ? &parts_[part_] : nullptr; }
    // Hacia dónde está el objetivo actual (falso si no hay).
    bool objective(Vec3& at) const {
        const LevelPart* p = currentPart();
        if (p) at = objectivePos(*p);
        return p != nullptr;
    }
    float transition() const { return transition_; }
    // Historia.
    const Dialogue& dialogue() const { return dialogue_; }
    bool dialogueBlocking() const { return dialogue_.blocking() || !shots_.empty(); }
    // Plano de escena en curso: desde dónde mira la cámara y a qué. Falso si no hay.
    bool cinematic(Vec3& eye, Vec3& focus) const;
    const Powers& powers() const { return powers_; }
    std::string speakerName(const std::string& id) const;
    // Sonidos emitidos desde la última llamada (los reproduce main con el audio).
    std::vector<SoundEvent> takeSounds() {
        std::vector<SoundEvent> out;
        out.swap(sounds_);
        return out;
    }
    bool inCombat() const;
    // Para la música de combate: algún enemigo (no los enjambres) va a por la protagonista cerca.
    bool fighting() const {
        for (const Enemy& e : enemies_)
            if (e.alive() && !isSwarm(e.kind) && e.state != AiState::Idle && e.state != AiState::Spawning &&
                length(e.body.pos - hero_.body.pos) < 25.0f)
                return true;
        return false;
    }
    bool chapterEnded() const { return chapterEnded_; }
    bool bossAlive() const {
        for (const Enemy& e : enemies_)
            if (e.boss && e.alive()) return true;
        return false;
    }
    // "El color es energía": 0 = ciudad gris y apagada, 1 = a todo color.
    float croma() const { return croma_; }
    // Apertura extra del campo de visión (radianes) en acercamientos y remates.
    float fovKick() const { return fovKick_; }
    Vec3 cameraKick() const { return camKick_; }  // tirón de cámara del último golpe (mundo)
    void setHitFx(unsigned mask) { hitFx_ = mask; }
    Vec3 heroRenderPos(float alpha) const { return hero_.interpolatedPos(alpha); }
    // Fracción [0,1] del segmento from→to libre de escenario (para que la cámara no
    // atraviese las casas).
    float cameraClear(Vec3 from, Vec3 to) const;

private:
    void buildPlaza();
    void buildMercado();
    void buildPuente();
    void buildPrueba();
    void buildSelva();
    void buildCueva();
    void buildCallejones();
    void buildCampo();
    void buildNeon();
    void buildCafetero();
    void buildRio();
    void buildCopas();
    // Mapas grandes: terreno de `size` m de lado con puntos cada `cell` m (alturas a cero).
    void makeTerrain(float size, float cell);
    // Pinta un punto del terreno (color y material del suelo).
    void paintTerrain(int ix, int iz, uint32_t color, Material m);
    // Pone una pieza (por su nombre de fichero). `onGround`: apoyada en el terreno (en su
    // punto más bajo, para que no flote en las cuestas); si no, a la altura de `pos`.
    // Devuelve falso si la pieza no existe.
    bool addProp(const std::string& name, Vec3 pos, float yaw = 0, float scale = 1, bool onGround = true);
    // Pone un habitante (por el nombre de su .glb) que pasea a `radius` m de `pos`.
    void entryBark(Npc& n);
    void addNpc(const std::string& model, Vec3 pos, float radius = 8, const std::string& name = "", NpcRole role = NpcRole::Townsfolk,
                std::vector<std::string> lines = {});
    // Los vecinos con nombre de cada escenario (tras montar el escenario y sus sólidos).
    void placeCitizens();
    void talkTo(Npc& n);
    void updateNpcs(float dt);
    // Grises salvajes (mapas abiertos): aparecen al azar por la zona; los lejanos y los ya
    // disipados se retiran de la lista (compactEnemies arregla los índices que la apuntan).
    void updateWild(float dt);
    // Enjambres (polillas de tinta, luciérnagas de cristal): su propio comportamiento.
    void updateSwarm(Enemy& e, float dt);
    void fireflyBurst(Vec3 c);
    std::vector<Spawn> deferredSpawns_;  // aparecen en el próximo paso (no en mitad de un recorrido de enemies_)
    void compactEnemies();
    void drawNpcs(Renderer& r, const ViewInfo& view, float alpha) const;
    void addTorch(Vec3 base);
    void addCrystal(Vec3 base, uint32_t color);
    void addPaperLantern(Vec3 a, Vec3 b, float y, uint32_t color);
    void addNeonSign(Vec3 pos, Vec3 out, uint32_t color);
    void updateTestMode(float dt);
    bool spawnTestEnemy();
    void addLampPost(Vec3 base, uint32_t color);
    void addBlock(Vec3 c, Vec3 h, uint32_t col, Material m = Material::Stone);
    void addDecor(const Mat4& t, Vec3 c, Vec3 h, uint32_t col, Material m, bool sphere);
    void addRim(Vec3 c, float hx, float hz, uint32_t col);
    void addBuilding(float cx, float cz, float hx, float hz, float height, Vec3 in, uint32_t tint, uint32_t neon);
    void addTree(Vec3 base, float size, uint32_t leaves);
    void addGate(const std::string& id, Vec3 c, Vec3 h, uint32_t color, bool closed);
    void updateLevel(float dt);
    bool partDone(const LevelPart& p) const;
    Vec3 objectivePos(const LevelPart& p) const;
    void setGate(const std::string& id, bool closed);
    void spawnEnemy(const Spawn& s);
    void resetEncounter();
    void traceBeams();
    // Interacción con lo más cercano (cápsula, nodo, farol, objeto, espejo). -1 si no hay nada.
    struct InteractTarget {
        int kind = -1;  // 0 cápsula, 1 nodo, 2 farol, 3 objeto, 4 espejo, 5 interruptor
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
    void updateRiposte(float dt);
    int riposteLength() const;
    Move riposteMove(int step) const;
    void updateEnemy(Enemy& e, float dt);
    Brain& brainFor(const Enemy& e);
    int heroActionCode() const;
    void decideIntent(Enemy& e, float dist);
    void enemyStartAttack(Enemy& e);
    void updateProjectiles(float dt);
    void explodeFire(Vec3 point);
    void updatePickups(float dt);
    void collect(Pickup& p);
    void dropLoot(Vec3 pos, int munny, int hpOrbs, int mpOrbs, ItemId item, bool withItem, bool shadow = false);
    bool heroWatches(const Enemy& e) const;
    void updateEffects(float dt);
    void breakCrate(Crate& c);
    void notify(const std::string& text, uint32_t color);
    void heal(float hp, float mp);
    void castImpact(Spell s);
    void supernova();
    bool startForm(Form f);
    void endForm();
    bool summon(int kind);
    void updateAllies(float dt, const Controls& c);
    void updateWolf(float dt);
    float formDamage() const;
    float formSpeed() const;
    void updateLight(float dt, const Controls& c);
    struct LightPoint {
        Vec3 pos;     // la luz
        Vec3 arrive;  // dónde se llega al viajar hasta ella
        int lamp = -1;  // farola (para gastar su energía), si lo es
    };
    void gatherLightPoints();
    void updateLightTarget(const Controls& c);
    void updateTravel(float dt);
    void updateInsideLight(float dt, const Controls& c);
    void startTravel(Vec3 to, int mode);
    void leaveLight(bool ambush);
    void cycleLightLock(Vec3 camForward);
    void updateShadow(float dt, const Controls& c);
    void swapMode();
    void drainLamp(int lamp, float amount);
    void updatePortals(float dt);
    void startDive();
    // Fundido en la sombra por las paredes: empujando contra una pared oscura, la sombra sube
    // por ella; arriba pasa al tejado y abajo vuelve al suelo.
    bool tryWallClimb(const Controls& c);
    void updateWallClimb(float dt, const Controls& c);
    void endDive(bool ambush);
    void constrainDive();
    void relightLamp(int lamp);
    Vec3 spellSpot(float range) const;  // dónde cae una técnica: el objetivo o delante
    void explodeBurst(Vec3 point);
    void updateBlackHoles(float dt);
    int nearLamp(float range, bool lit) const;
    float familyDamage(const Enemy& e) const;
    int ambushTarget() const;
    void spawnSparks(Vec3 pos, Vec3 dir, int count, uint32_t color, float speed, bool stars = true);
    void spawnRing(Vec3 pos, float radius, float life, uint32_t color);
    // Efectos de un golpe cuerpo a cuerpo de la protagonista (tajo, remate) y de cualquier
    // golpe que empuje a un enemigo (destello, materia, sacudida, cámara).
    void meleeHitFx(const Enemy& e, Move m, Vec3 dir);
    void impactFx(Enemy& e, Vec3 point, Vec3 dir, float strength);
    void updateHitFx(float dt);
    void drawHitFx(DebugDraw& glow, Renderer& solid, const ViewInfo& view) const;
    float groundHeight(Vec3 p) const;
    Vec3 bladeTip() const;
    float random01();
    float randRange(float a, float b) { return a + (b - a) * random01(); }

    void playScene(const std::string& id);
    bool cinemaCommand(const StoryLine& cmd);
    void spawnEcho(const std::string& name);
    void updateCinema(float dt, const Controls& c);
    Controls filterControls(const Controls& c) const;
    void drawGhosts(Renderer* solid, DebugDraw& lines, float alpha) const;
    void drawCinemaHud(UiLayer& ui, const ViewInfo& view) const;
    void storyCommand(const StoryLine& cmd);
    void updateStory(float dt);
    void updateBrizna(float dt);
    void sound(Sfx id, Vec3 pos, float gain = 1, float pitch = 1);
    // Voz de la protagonista: la de Vela en la luz y la de Hollín en la sombra. `chance`: con
    // qué probabilidad suena (los golpes normales no siempre gritan).
    enum class VoiceLine { Attack, Finisher, Jump, Hurt, Dodge, Laugh };
    void voice(VoiceLine line, float chance = 1);
    void uiSound(Sfx id, float gain = 1, float pitch = 1);
    void defeatHero();
    void respawnHero();

    // Dibujado (game_draw.cpp).
    void drawLevel(DebugDraw& lines, Renderer* solid, const DrawFlags& f, const ViewInfo& view, float alpha) const;
    void drawTerrain(Renderer& r) const;
    void drawProps(Renderer& r, const ViewInfo& view) const;
    void drawHero(DebugDraw& lines, Renderer* solid, const DrawFlags& f, float alpha) const;
    void drawEnemies(DebugDraw& lines, DebugDraw& glow, Renderer* solid, const DrawFlags& f, const ViewInfo& view,
                     float alpha) const;
    void drawEffects(DebugDraw& lines, DebugDraw& glow, Renderer* solid, const ViewInfo& view) const;
    void drawStory(DebugDraw& lines, DebugDraw& glow, Renderer* solid, const ViewInfo& view) const;
    void drawStoryHud(UiLayer& ui, const ViewInfo& view) const;

    World world_;
    World cameraWorld_;  // lo que tapa la cámara (sin las barreras translúcidas)
    std::vector<Block> blocks_;  // escenario fijo (world_ = esto + cajas + cofres)
    std::vector<Decor> decor_;
    std::vector<PropInstance> props_;
    std::vector<Npc> npcs_;
    // Lo último que ha dicho un vecino (subtítulo que no para el juego).
    std::string npcSpeaker_, npcLine_;
    float npcLineTime_ = 0;
    bool shopRequest_ = false;  // la Doctora Umbra abre el taller (lo recoge main)
    std::vector<AABB> propSolids_;  // colisiones de las piezas (entran en world_)
    std::vector<uint32_t> terrainColor_;  // por punto del terreno
    std::vector<uint8_t> terrainMaterial_;
    uint32_t terrainVersion_ = 0;            // cambia al hacer otro terreno (hay que volver a subirlo)
    mutable uint32_t terrainUploaded_ = 0;
    mutable int terrainMesh_ = -1;
    float fogScale_ = 1;        // niebla del escenario (los abiertos, menos)
    bool wildSpawns_ = false;     // salen Grises al azar por la zona (campo)
    bool wildOnlyMoths_ = false;  // solo enjambres de polillas (la ciudad)
    float wildTimer_ = 0;
    std::vector<Vec3> wildSafe_;  // ahí no aparecen (pueblos)
    float darkScale_ = 1;       // cuánto oscurece la oscuridad (en los mapas abiertos, menos: se ve el paisaje)
    bool propScenery_ = true;   // las piezas, como escenario (la Ciudad Neón, con su sombreado propio)
    float viewDistance_ = 200;  // hasta dónde se dibuja
    std::vector<Lamp> lamps_;
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
    std::vector<LightEmitter> emitters_;
    std::vector<Mirror> mirrors_;
    std::vector<Receptor> receptors_;
    std::vector<std::vector<Vec3>> beamPaths_;     // recorrido de cada haz (para dibujarlo)
    std::vector<std::vector<uint32_t>> beamColors_;  // color de cada tramo
    int activeEncounter_ = -1;
    Vec3 checkpoint_;
    float checkpointYaw_ = 0;
    LevelId nextLevel_ = LevelId::Plaza;
    bool lastLevel_ = false;
    float transition_ = 0;     // fundido a negro al cambiar de escenario (0..1)
    bool leaving_ = false;
    uint32_t floorColor_ = 0;
    float night_ = 0;  // escenario de noche (0..1): cielo oscuro, solo alumbran sus luces
    bool daylight_ = false;     // de día (mapas abiertos): la sombra es la que hace el sol
    float waterLevel_ = -1000;  // agua (ríos) por debajo de esta altura; muy abajo: sin agua
    Material floorMaterial_ = Material::Ground;
    Character hero_;
    Inventory inv_;
    float hp_ = 100, maxHp_ = 100;
    int heroLevel_ = 1, xp_ = 0;
    float levelFlash_ = 0;  // destello del HUD al subir de nivel
    // Poder: la única barra de las técnicas de luz y de sombra. No se regenera sola: se llena
    // con pociones de poder y con los orbes que sueltan los Grises.
    float mp_ = 40, maxMp_ = 100;
    float hpGhost_ = 100;     // barra roja que va detrás al perder vida
    float heroInvuln_ = 0;    // tras un golpe, un momento sin recibir otro
    float heroFlash_ = 0;
    float guardFlash_ = 0;    // destello de bloqueo (perfecto = más fuerte)
    bool perfectGuard_ = false;
    float voiceCooldown_ = 0;
    Move voicedMove_ = Move::None;  // el último golpe que ya ha tenido su grito
    // Súper contraataque: tras un bloqueo perfecto el atacante queda aturdido y, si se ataca
    // a tiempo, el protagonista encadena solo una ráfaga de golpes con el doble de daño.
    float riposteWindow_ = 0;
    int riposteTarget_ = -1, riposteStep_ = -1;
    Spell pendingSpell_ = Spell::Fire;  // la que sale al llegar el impacto del Cast
    float novaFlash_ = 0;
    std::vector<LightPoint> lightPoints_;
    int lightTarget_ = -1;
    bool travel_ = false;
    int travelMode_ = 0;      // 0 entrar o saltar a una fuente, 1 salir, 2 emboscada
    int ambushEnemy_ = -1;
    bool inLight_ = false;    // dentro de una fuente de luz
    int hostLamp_ = -1, travelLamp_ = -1;  // farola en la que está / a la que va
    Vec3 hostPos_;            // la fuente en la que está
    bool lightLock_ = false;  // fuente elegida con R estando dentro
    bool shadowMode_ = false, shadowUnlocked_ = false;
    static constexpr float kModeSwapTime = 0.6f;  // lo que dura la transformación al cambiar de modo
    float swapCooldown_ = 0, modeFlash_ = 0;
    bool diving_ = false;      // fundido con la sombra del suelo
    bool onWall_ = false;      // ...o de una pared
    int wallBox_ = -1;         // el bloque por el que trepa
    Vec3 wallNormal_;          // hacia fuera de la cara
    float wallBase_ = 0;       // altura del pie de la pared
    Vec3 diveFrom_, lastDark_;
    std::vector<Portal> portals_;
    float darkShown_ = 0;  // cuánto se oscurece en pantalla lo que no alumbra ninguna luz
    std::vector<LampSwitch> switches_;
    void placeLampSwitches();
    void toggleSwitch(int i);
    void hitLamp(int i, float damage);
    void updateLamps(float dt);
    std::vector<BlackHole> holes_;
    Vec3 lightLockPos_;
    float lightHint_ = 0;
    Vec3 travelFrom_, travelTo_;
    float travelTime_ = 0, travelDur_ = 0;
    int commandSel_ = 0;
    int lock_ = -1;           // enemigo fijado
    std::vector<Enemy> enemies_;
    std::vector<Projectile> projectiles_;
    std::vector<Pickup> pickups_;
    static constexpr size_t kMaxParticles = 900;
    std::vector<Particle> particles_;
    std::vector<Ring> rings_;
    std::vector<SlashMark> slashes_;
    std::vector<ImpactFlash> flashes_;
    std::vector<Debris> debris_;
    std::vector<InkSplat> splats_;
    unsigned hitFx_ = kFxAll;
    Vec3 camKick_, camKickVel_;
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
    Powers powers_;
    std::deque<Shot> shots_;
    float shotTime_ = 0, cineBars_ = 0;
    std::vector<Ghost> ghosts_;
    std::deque<Tutorial> tutorials_;
    float walkTime_ = 0;  // para el tutorial de andar
    Vec3 briznaPos_, briznaVel_;
    std::vector<SoundEvent> sounds_;
    Brain brains_[int(EnemyKind::Count)];
    Brain bossBrain_;
    Form form_ = Form::None;
    float formTime_ = 0;
    Enemy wolf_;  // el cuerpo del Lobo de sombra: un Sabueso grande que sigue al protagonista
    std::vector<Companion> companions_;
    float noraCooldown_ = 0;
    std::string brainDir_;
    float adaptNotice_ = 0;
    float croma_ = 0.12f;        // sube al disipar Grises y al superar oleadas
    float cromaShown_ = 0.12f;   // el que se ve: lo sigue suavemente
    std::vector<ColorSplash> splashes_;
    int kills_ = 0;
    float combatTimer_ = 0;   // > 0: el arma sigue en la mano
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
    // Modos de prueba.
    TestMode testMode_ = TestMode::None;
    uint32_t testSeed_ = 0;
    float testArena_ = 20;       // medio lado del escenario de combate
    float testSpawnTimer_ = 0;
    bool devNoSpawns_ = false;
    int testBossAt_ = 30;        // bajas con las que aparece el siguiente Gris mayor
    // Trucos sin historia (F1, F2 o el menú de pausa): por defecto solo la vida es infinita,
    // para poder probar la gestión del Poder.
    bool infHp_ = true, infPower_ = false;
    bool freePlay_ = false;  // nivel suelto elegido en el selector (sin guion)

};
