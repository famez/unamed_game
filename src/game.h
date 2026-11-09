#pragma once

// Estado y reglas del juego: nivel, jugador, enemigos, proyectiles y partículas.
// game.cpp contiene la lógica; game_draw.cpp el dibujado (mundo 3D y HUD).

#include "character.h"
#include "debug_draw.h"
#include "physics.h"
#include "renderer.h"
#include "skeleton.h"
#include "vmath.h"

#include <algorithm>
#include <cstdint>
#include <vector>

// Geometría de los objetos, en espacio local de la muñeca derecha (la mano apunta a -Y).
constexpr Vec3 kPistolMuzzle{0, -0.22f, 0.035f};
constexpr Vec3 kRockInHand{0, -0.09f, 0.02f};
// Escudo de los enemigos, en espacio local del pecho.
constexpr Vec3 kShieldCenter{0.06f, 0.06f, 0.30f};
constexpr Vec3 kShieldHalf{0.26f, 0.34f, 0.025f};

constexpr int kMaxRocks = 20;
constexpr int kMaxMolotovs = 10;

struct DrawFlags {
    bool bones = true;
    bool joints = true;
    bool body = true;
    bool grid = true;
};

// Órdenes del jugador para un paso de simulación. Los flancos (saltar, usar...) valen
// true en un único paso.
struct Controls {
    Vec3 move;  // dirección deseada en el mundo (XZ), longitud 0..1
    bool run = false;
    bool jump = false;
    bool use = false;        // usar el objeto seleccionado
    bool throwRock = false;  // lanzar piedra sin cambiar de objeto
    int selectSlot = -1;     // -1 = sin cambio
    float aimYaw = 0;        // hacia dónde mira la cámara
    float aimPitch = 0;      // inclinación de la cámara (positivo = mirando hacia abajo)
    bool aim = false;        // botón derecho mantenido
    Vec3 aimOrigin;          // rayo de la mira: posición de la cámara...
    Vec3 aimDir{0, 0, 1};    // ...y dirección por el centro de la pantalla
};

struct Enemy {
    Body body;
    float yaw = 0;
    Vec3 spawn;
    float spawnYaw = 0;
    float health = 100, maxHealth = 100;
    float hitTimer = 0;    // tambaleo tras un impacto
    float blockTimer = 0;  // destello del escudo
    bool hitFromFront = false;
    float deadTimer = -1;  // >= 0: muerto (segundos desde la muerte)
    float burnTimer = 0;   // > 0: está ardiendo
    float breathOffset = 0;
    Pose pose;
    Mat4 joints[JointCount] = {};

    bool alive() const { return deadTimer < 0; }
    Vec3 forward() const { return {std::sin(yaw), 0, std::cos(yaw)}; }
    AABB hitBox() const { return body.bounds(); }
    Mat4 modelMatrix() const;
};

enum class ProjectileKind { Bullet, Rock, Molotov };

struct Projectile {
    ProjectileKind kind;
    Vec3 pos, vel;
    float life = 0;       // balas: segundos que les quedan
    bool armed = true;    // hace daño (las piedras dejan de hacerlo al frenarse)
    bool resting = false; // piedra parada en el suelo: se puede recoger
    bool dead = false;
    float spin = 0;       // giro del cóctel en vuelo
};

struct Particle {
    Vec3 pos, vel;
    float life;
    uint32_t color;
    float gravity = 1.0f;  // negativo = asciende (ascuas)
};

// Charco de fuego de un cóctel molotov. Las "lenguas" son los puntos donde se dibujan
// llamas; solo existen donde hay suelo debajo (no flotan al borde de una plataforma).
struct FireTongue {
    Vec3 pos;
    float dist;  // distancia al centro: el fuego se extiende de dentro afuera
    float seed, size;
};

struct Fire {
    Vec3 pos;
    float maxRadius = 2.6f;
    float age = 0, duration = 7.0f;
    float seed = 0;
    std::vector<FireTongue> tongues;

    float radius() const { return maxRadius * smoothstep01(age / 0.35f); }
    // 0..1: aparece en 0,15 s y se apaga durante los últimos 1,5 s.
    float strength() const { return std::min(1.0f, age / 0.15f) * clampf((duration - age) / 1.5f, 0, 1); }
};

struct SmokePuff {
    Vec3 pos, vel;
    float age, life, seed;
};

struct Blast {  // bola de fuego de la explosión
    Vec3 pos;
    float age, seed;
};

struct ScorchMark {  // suelo chamuscado
    Vec3 pos;
    float radius, age;
};

const char* itemName(Item item);

class Game {
public:
    void init();
    void update(float dt, const Controls& c);
    void resetHero();

    // Con `solid` (render con shaders) las cajas van ahí y `dd` recibe solo los efectos
    // (balas, láser, chispas, barras de vida). Con nullptr, todo en alambre (modo debug).
    void draw(DebugDraw& dd, Renderer* solid, const DrawFlags& flags, Vec3 cameraRight) const;
    void drawHud(DebugDraw& hud, int width, int height) const;

    const Character& hero() const { return hero_; }
    int rocks() const { return rocks_; }
    int molotovs() const { return molotovs_; }
    float time() const { return time_; }
    int kills() const { return kills_; }

private:
    void addEnemy(Vec3 pos, float yaw);
    void drawFire(DebugDraw& dd, Renderer* solid, Vec3 cameraRight) const;
    void useItem(Item item, float aimYaw, float aimPitch);
    void resolveImpact(Action a);
    void meleeHit(float range, float cosHalfArc, float damage, float knock, float lift);
    void hitEnemy(Enemy& e, Vec3 dir, Vec3 point, float damage, float knock, float lift);
    void updateEnemy(Enemy& e, float dt);
    void updateProjectiles(float dt);
    void updateParticles(float dt);
    void updateFires(float dt);
    void igniteAt(Vec3 point, Vec3 normal);
    void damageEnemy(Enemy& e, float damage);
    Item handItem() const;  // lo que se ve en la mano (Count = nada)
    void updateAimPoint(Vec3 origin, Vec3 dir);
    void spawnSparks(Vec3 pos, Vec3 dir, int count, uint32_t color, float speed);
    float random01();

    World world_;
    Character hero_;
    std::vector<Enemy> enemies_;
    std::vector<Projectile> projectiles_;
    std::vector<Particle> particles_;
    int rocks_ = 10;
    int molotovs_ = 5;
    Item throwItem_ = Item::Rocks;  // qué se está lanzando (piedra o cóctel)
    std::vector<Fire> fires_;
    std::vector<SmokePuff> smoke_;
    std::vector<Blast> blasts_;
    std::vector<ScorchMark> scorches_;
    int kills_ = 0;
    float throwUp_ = 0;  // velocidad vertical del lanzamiento en curso (según la cámara)
    Vec3 aimPoint_;            // dónde impactaría una bala disparada con la mira
    bool aimOnEnemy_ = false;  // la mira está sobre un enemigo
    float time_ = 0;
    uint32_t rng_ = 12345;
};
