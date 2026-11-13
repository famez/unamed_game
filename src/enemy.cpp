#include "enemy.h"

namespace {

constexpr uint32_t rgb(uint8_t r, uint8_t g, uint8_t b) {
    return uint32_t(r) | uint32_t(g) << 8 | uint32_t(b) << 16 | 0xFF000000u;
}

// --- Rastrero: pequeño, cabezón, orejas largas; corre a saltitos y se lanza con las garras.
enum ImpJoint { ImPelvis, ImBody, ImHead, ImEarL, ImEarR, ImArmL, ImHandL, ImArmR, ImHandR, ImLegL, ImFootL,
                ImLegR, ImFootR, ImTail, ImCount };
// clang-format off
const JointDef kImpRig[ImCount] = {
//   nombre     padre    offset                     centro caja            semiejes caja
    {"pelvis",  -1,      {0.00f, 0.42f, 0.00f},    {0, 0.00f, 0},         {0.16f, 0.10f, 0.13f}},
    {"body",    ImPelvis,{0.00f, 0.06f, 0.00f},    {0, 0.14f, 0},         {0.20f, 0.18f, 0.17f}},
    {"head",    ImBody,  {0.00f, 0.32f, 0.02f},    {0, 0.15f, 0.02f},     {0.22f, 0.19f, 0.20f}},
    {"ear_l",   ImHead,  {0.12f, 0.28f, -0.02f},   {0, 0.14f, 0},         {0.04f, 0.14f, 0.03f}},
    {"ear_r",   ImHead,  {-0.12f, 0.28f, -0.02f},  {0, 0.14f, 0},         {0.04f, 0.14f, 0.03f}},
    {"arm_l",   ImBody,  {0.19f, 0.22f, 0.00f},    {0, -0.10f, 0},        {0.04f, 0.10f, 0.04f}},
    {"hand_l",  ImArmL,  {0.00f, -0.21f, 0.00f},   {0, -0.06f, 0.01f},    {0.06f, 0.07f, 0.05f}},
    {"arm_r",   ImBody,  {-0.19f, 0.22f, 0.00f},   {0, -0.10f, 0},        {0.04f, 0.10f, 0.04f}},
    {"hand_r",  ImArmR,  {0.00f, -0.21f, 0.00f},   {0, -0.06f, 0.01f},    {0.06f, 0.07f, 0.05f}},
    {"leg_l",   ImPelvis,{0.10f, -0.04f, 0.00f},   {0, -0.15f, 0},        {0.05f, 0.15f, 0.05f}},
    {"foot_l",  ImLegL,  {0.00f, -0.30f, 0.00f},   {0, -0.03f, 0.05f},    {0.07f, 0.04f, 0.11f}},
    {"leg_r",   ImPelvis,{-0.10f, -0.04f, 0.00f},  {0, -0.15f, 0},        {0.05f, 0.15f, 0.05f}},
    {"foot_r",  ImLegR,  {0.00f, -0.30f, 0.00f},   {0, -0.03f, 0.05f},    {0.07f, 0.04f, 0.11f}},
    {"tail",    ImPelvis,{0.00f, 0.00f, -0.14f},   {0, 0, -0.15f},        {0.03f, 0.03f, 0.15f}},
};
// Los Grises: cuerpo sin color (gris y carbón) y, como única luz, el color que han robado.
const uint32_t kImpSkin = rgb(150, 152, 166), kImpBelly = rgb(96, 98, 112), kImpDark = rgb(56, 58, 70);
const uint32_t kImpEye = rgb(255, 70, 190), kImpPink = rgb(255, 70, 190);
const EnemyShape kImpShapes[] = {
    {ImPelvis, {0, 0, 0},             {0.17f, 0.12f, 0.14f},  kImpSkin, false},
    {ImBody,   {0, 0.14f, 0},         {0.21f, 0.20f, 0.18f},  kImpSkin, false},
    {ImBody,   {0, 0.11f, 0.08f},     {0.15f, 0.15f, 0.12f},  kImpBelly, false},
    {ImHead,   {0, 0.15f, 0.02f},     {0.24f, 0.21f, 0.22f},  kImpSkin, false},
    {ImHead,   {0.085f, 0.17f, 0.2f}, {0.06f, 0.075f, 0.035f}, kImpEye, true},
    {ImHead,   {-0.085f, 0.17f, 0.2f},{0.06f, 0.075f, 0.035f}, kImpEye, true},
    {ImHead,   {0, 0.06f, 0.215f},    {0.08f, 0.018f, 0.02f}, kImpDark, false},  // boca
    {ImEarL,   {0, 0.13f, 0},         {0.05f, 0.15f, 0.035f}, kImpSkin, false},
    {ImEarL,   {0, 0.27f, 0},         {0.035f, 0.05f, 0.03f}, kImpPink, true},
    {ImEarR,   {0, 0.13f, 0},         {0.05f, 0.15f, 0.035f}, kImpSkin, false},
    {ImEarR,   {0, 0.27f, 0},         {0.035f, 0.05f, 0.03f}, kImpPink, true},
    {ImArmL,   {0, -0.10f, 0},        {0.05f, 0.12f, 0.05f},  kImpSkin, false},
    {ImHandL,  {0, -0.06f, 0.01f},    {0.065f, 0.075f, 0.055f}, kImpDark, false},
    {ImArmR,   {0, -0.10f, 0},        {0.05f, 0.12f, 0.05f},  kImpSkin, false},
    {ImHandR,  {0, -0.06f, 0.01f},    {0.065f, 0.075f, 0.055f}, kImpDark, false},
    {ImLegL,   {0, -0.14f, 0},        {0.06f, 0.16f, 0.06f},  kImpSkin, false},
    {ImFootL,  {0, -0.03f, 0.06f},    {0.075f, 0.05f, 0.12f}, kImpDark, false},
    {ImLegR,   {0, -0.14f, 0},        {0.06f, 0.16f, 0.06f},  kImpSkin, false},
    {ImFootR,  {0, -0.03f, 0.06f},    {0.075f, 0.05f, 0.12f}, kImpDark, false},
    {ImTail,   {0, 0, -0.15f},        {0.03f, 0.03f, 0.16f},  kImpSkin, false},
    {ImTail,   {0, 0, -0.32f},        {0.06f, 0.06f, 0.06f},  kImpPink, true},
};

// --- Domo: cúpula enorme sobre patas cortas; salta y cae aplastando (onda alrededor).
enum ShroomJoint { ShRoot, ShStem, ShCap, ShArmL, ShArmR, ShLegL, ShFootL, ShLegR, ShFootR, ShCount };
const JointDef kShroomRig[ShCount] = {
    {"root",    -1,      {0.00f, 0.32f, 0.00f},    {0, 0, 0},             {0.20f, 0.10f, 0.18f}},
    {"stem",    ShRoot,  {0.00f, 0.06f, 0.00f},    {0, 0.20f, 0},         {0.22f, 0.24f, 0.20f}},
    {"cap",     ShStem,  {0.00f, 0.46f, 0.00f},    {0, 0.10f, 0},         {0.50f, 0.18f, 0.50f}},
    {"arm_l",   ShStem,  {0.24f, 0.22f, 0.00f},    {0, -0.08f, 0},        {0.04f, 0.09f, 0.04f}},
    {"arm_r",   ShStem,  {-0.24f, 0.22f, 0.00f},   {0, -0.08f, 0},        {0.04f, 0.09f, 0.04f}},
    {"leg_l",   ShRoot,  {0.11f, -0.04f, 0.00f},   {0, -0.11f, 0},        {0.06f, 0.11f, 0.06f}},
    {"foot_l",  ShLegL,  {0.00f, -0.22f, 0.00f},   {0, -0.03f, 0.04f},    {0.08f, 0.05f, 0.11f}},
    {"leg_r",   ShRoot,  {-0.11f, -0.04f, 0.00f},  {0, -0.11f, 0},        {0.06f, 0.11f, 0.06f}},
    {"foot_r",  ShLegR,  {0.00f, -0.22f, 0.00f},   {0, -0.03f, 0.04f},    {0.08f, 0.05f, 0.11f}},
};
const uint32_t kShroomStem = rgb(172, 174, 186), kShroomCap = rgb(78, 80, 96), kShroomSpot = rgb(255, 185, 50);
const uint32_t kShroomGill = rgb(120, 122, 136), kShroomDark = rgb(40, 42, 52), kShroomShoe = rgb(60, 62, 74);
const EnemyShape kShroomShapes[] = {
    {ShRoot,  {0, 0, 0},               {0.20f, 0.13f, 0.18f},  kShroomStem, false},
    {ShStem,  {0, 0.20f, 0},           {0.25f, 0.30f, 0.23f},  kShroomStem, false},
    {ShStem,  {0.08f, 0.28f, 0.20f},   {0.045f, 0.065f, 0.03f}, kShroomDark, false},  // ojos
    {ShStem,  {-0.08f, 0.28f, 0.20f},  {0.045f, 0.065f, 0.03f}, kShroomDark, false},
    {ShStem,  {0.09f, 0.30f, 0.225f},  {0.015f, 0.018f, 0.01f}, rgb(255, 255, 255), true},  // brillo
    {ShStem,  {-0.07f, 0.30f, 0.225f}, {0.015f, 0.018f, 0.01f}, rgb(255, 255, 255), true},
    {ShStem,  {0, 0.19f, 0.215f},      {0.05f, 0.025f, 0.02f}, kShroomSpot, true},  // boca
    {ShCap,   {0, 0.00f, 0},           {0.46f, 0.07f, 0.46f},  kShroomGill, false},
    {ShCap,   {0, 0.10f, 0},           {0.56f, 0.25f, 0.56f},  kShroomCap, false},
    {ShCap,   {0.0f, 0.33f, 0.0f},     {0.10f, 0.04f, 0.10f},  kShroomSpot, true},
    {ShCap,   {0.30f, 0.22f, 0.18f},   {0.09f, 0.05f, 0.09f},  kShroomSpot, true},
    {ShCap,   {-0.28f, 0.22f, 0.20f},  {0.08f, 0.05f, 0.08f},  kShroomSpot, true},
    {ShCap,   {0.10f, 0.24f, -0.32f},  {0.09f, 0.05f, 0.09f},  kShroomSpot, true},
    {ShCap,   {-0.25f, 0.25f, -0.18f}, {0.07f, 0.05f, 0.07f},  kShroomSpot, true},
    {ShCap,   {0.02f, 0.22f, 0.38f},   {0.07f, 0.05f, 0.06f},  kShroomSpot, true},
    {ShArmL,  {0, -0.08f, 0},          {0.05f, 0.10f, 0.05f},  kShroomStem, false},
    {ShArmR,  {0, -0.08f, 0},          {0.05f, 0.10f, 0.05f},  kShroomStem, false},
    {ShLegL,  {0, -0.10f, 0},          {0.065f, 0.12f, 0.065f}, kShroomStem, false},
    {ShFootL, {0, -0.03f, 0.04f},      {0.09f, 0.06f, 0.12f},  kShroomShoe, false},
    {ShLegR,  {0, -0.10f, 0},          {0.065f, 0.12f, 0.065f}, kShroomStem, false},
    {ShFootR, {0, -0.03f, 0.04f},      {0.09f, 0.06f, 0.12f},  kShroomShoe, false},
};

// --- Eco: vuela en círculos a distancia y dispara orbes de luz robada.
enum MothJoint { MoThorax, MoHead, MoAntL, MoAntR, MoWingL, MoWingR, MoWingL2, MoWingR2, MoAbdomen, MoCount };
const JointDef kMothRig[MoCount] = {
    {"thorax",  -1,       {0.00f, 0.30f, 0.00f},   {0, 0, 0},             {0.12f, 0.12f, 0.14f}},
    {"head",    MoThorax, {0.00f, 0.06f, 0.17f},   {0, 0, 0.02f},         {0.11f, 0.10f, 0.10f}},
    {"ant_l",   MoHead,   {0.05f, 0.08f, 0.03f},   {0, 0.12f, 0.03f},     {0.015f, 0.12f, 0.015f}},
    {"ant_r",   MoHead,   {-0.05f, 0.08f, 0.03f},  {0, 0.12f, 0.03f},     {0.015f, 0.12f, 0.015f}},
    {"wing_l",  MoThorax, {0.10f, 0.04f, 0.02f},   {0.32f, 0, 0},         {0.32f, 0.01f, 0.22f}},
    {"wing_r",  MoThorax, {-0.10f, 0.04f, 0.02f},  {-0.32f, 0, 0},        {0.32f, 0.01f, 0.22f}},
    {"wing_l2", MoThorax, {0.08f, -0.02f, -0.08f}, {0.20f, 0, -0.10f},    {0.20f, 0.01f, 0.14f}},
    {"wing_r2", MoThorax, {-0.08f, -0.02f, -0.08f},{-0.20f, 0, -0.10f},   {0.20f, 0.01f, 0.14f}},
    {"abdomen", MoThorax, {0.00f, -0.02f, -0.14f}, {0, -0.02f, -0.15f},   {0.10f, 0.10f, 0.18f}},
};
const uint32_t kMothFur = rgb(196, 198, 210), kMothBody = rgb(80, 84, 100), kMothWing = rgb(140, 142, 158);
const uint32_t kMothWing2 = rgb(110, 112, 128), kMothEye = rgb(40, 225, 255);
const EnemyShape kMothShapes[] = {
    {MoThorax,  {0, 0, 0},               {0.14f, 0.14f, 0.16f},  kMothFur, false},
    {MoHead,    {0, 0, 0.02f},           {0.12f, 0.11f, 0.11f},  kMothBody, false},
    {MoHead,    {0.065f, 0.02f, 0.09f},  {0.05f, 0.06f, 0.04f},  kMothEye, true},
    {MoHead,    {-0.065f, 0.02f, 0.09f}, {0.05f, 0.06f, 0.04f},  kMothEye, true},
    {MoAntL,    {0, 0.12f, 0.03f},       {0.015f, 0.12f, 0.015f}, kMothBody, false},
    {MoAntL,    {0, 0.25f, 0.05f},       {0.04f, 0.04f, 0.04f},  kMothEye, true},
    {MoAntR,    {0, 0.12f, 0.03f},       {0.015f, 0.12f, 0.015f}, kMothBody, false},
    {MoAntR,    {0, 0.25f, 0.05f},       {0.04f, 0.04f, 0.04f},  kMothEye, true},
    {MoWingL,   {0.32f, 0, 0},           {0.33f, 0.012f, 0.23f}, kMothWing, false},
    {MoWingL,   {0.42f, 0.006f, 0.04f},  {0.08f, 0.01f, 0.08f},  kMothEye, true},
    {MoWingR,   {-0.32f, 0, 0},          {0.33f, 0.012f, 0.23f}, kMothWing, false},
    {MoWingR,   {-0.42f, 0.006f, 0.04f}, {0.08f, 0.01f, 0.08f},  kMothEye, true},
    {MoWingL2,  {0.20f, 0, -0.10f},      {0.21f, 0.012f, 0.15f}, kMothWing2, false},
    {MoWingR2,  {-0.20f, 0, -0.10f},     {0.21f, 0.012f, 0.15f}, kMothWing2, false},
    {MoAbdomen, {0, -0.02f, -0.15f},     {0.11f, 0.11f, 0.19f},  kMothBody, false},
    {MoAbdomen, {0, -0.02f, -0.08f},     {0.115f, 0.03f, 0.05f}, kMothFur, false},
    {MoAbdomen, {0, -0.02f, -0.20f},     {0.10f, 0.03f, 0.05f},  kMothFur, false},
};

#define COUNT(a) int(sizeof(a) / sizeof((a)[0]))
const EnemyDef kDefs[int(EnemyKind::Count)] = {
//   nombre          rig        n        formas        n                  vida  vel.  radio alto  aggro alc.  prep. ataque recup. espera daño golpe vuela altura munny
    {"RASTRERO",     kImpRig,    ImCount, kImpShapes,    COUNT(kImpShapes),    30, 4.2f, 0.35f, 1.0f, 16, 2.6f, 0.45f, 0.40f, 0.60f, 0.9f,  8, 5.0f, false, 0,    3, 6},
    {"DOMO",          kShroomRig, ShCount, kShroomShapes, COUNT(kShroomShapes), 60, 2.4f, 0.50f, 1.2f, 14, 3.2f, 0.60f, 1.00f, 0.80f, 1.5f, 14, 8.0f, false, 0,    6, 10},
    {"ECO",          kMothRig,   MoCount, kMothShapes,   COUNT(kMothShapes),   24, 3.2f, 0.40f, 0.6f, 18, 8.0f, 0.70f, 0.30f, 0.80f, 1.6f, 10, 4.0f, true,  2.3f, 4, 8},
};
#undef COUNT
// clang-format on

static_assert(ImCount <= kMaxJoints && ShCount <= kMaxJoints && MoCount <= kMaxJoints, "rig demasiado grande");

void animateImp(Enemy& e, float time, Vec3* r, Vec3& root) {
    float s = std::sin(e.phase), c = std::cos(e.phase);
    float t = e.stateTime;
    // Base: respiración y orejas que se mueven.
    r[ImBody].x = 0.04f * std::sin(time * 3.0f + e.seed);
    r[ImEarL].z = -0.35f + 0.1f * std::sin(time * 2.0f + e.seed);
    r[ImEarR].z = 0.35f - 0.1f * std::sin(time * 2.3f + e.seed);
    r[ImEarL].x = r[ImEarR].x = 0.3f;
    r[ImArmL].z = 0.3f;
    r[ImArmR].z = -0.3f;
    r[ImTail].x = -0.4f + 0.2f * std::sin(time * 4.0f + e.seed);
    r[ImTail].y = 0.3f * std::sin(time * 3.0f + e.seed);
    switch (e.state) {
        case AiState::Chase:
        case AiState::Idle: {
            // Carrerita a saltos: piernas alternas, cuerpo inclinado y botando.
            float run = e.state == AiState::Chase ? 1.0f : 0.4f;
            r[ImLegL].x = -0.8f * s * run;
            r[ImLegR].x = 0.8f * s * run;
            r[ImArmL].x = 0.9f * s * run;
            r[ImArmR].x = -0.9f * s * run;
            r[ImPelvis].x = 0.3f * run;
            r[ImHead].x = -0.2f * run;
            r[ImEarL].x += 0.6f * run;
            r[ImEarR].x += 0.6f * run;
            root.y = 0.06f * std::fabs(c) * run;
            break;
        }
        case AiState::Windup: {
            // Se agacha con las garras en alto y tiembla: aviso del ataque.
            float w = smoothstep01(t / 0.2f);
            root.y = -0.12f * w;
            r[ImPelvis].x = -0.25f * w;
            r[ImLegL].x = r[ImLegR].x = -0.7f * w;
            r[ImFootL].x = r[ImFootR].x = 0.7f * w;
            r[ImArmL].x = r[ImArmR].x = -2.6f * w;
            r[ImArmL].z = 0.6f;
            r[ImArmR].z = -0.6f;
            r[ImBody].y = 0.08f * std::sin(time * 60.0f);
            r[ImEarL].z -= 0.4f * w;
            r[ImEarR].z += 0.4f * w;
            break;
        }
        case AiState::Attack: {
            // Lanzado hacia delante, zarpazo con los dos brazos.
            float u = clampf(t / 0.3f, 0, 1);
            r[ImPelvis].x = 0.9f;
            r[ImHead].x = -0.6f;
            r[ImLegL].x = r[ImLegR].x = 0.9f;
            r[ImArmL].x = r[ImArmR].x = lerpf(-2.6f, -0.6f, smoothstep01(u));
            r[ImArmL].z = 0.2f;
            r[ImArmR].z = -0.2f;
            r[ImEarL].x = r[ImEarR].x = 1.2f;
            break;
        }
        case AiState::Recover:
            r[ImPelvis].x = 0.3f;
            r[ImHead].x = 0.2f;
            r[ImArmL].z = r[ImArmR].z = 0;
            r[ImArmL].x = r[ImArmR].x = 0.2f;
            break;
        case AiState::Stagger: {
            float w = std::sin(clampf(t / 0.45f, 0, 1) * kPi);
            r[ImPelvis].x = -0.6f * w;
            r[ImHead].x = -0.5f * w;
            r[ImArmL].z = 1.4f * w;
            r[ImArmR].z = -1.4f * w;
            r[ImLegL].x = -0.5f * w;
            r[ImEarL].z -= 0.8f * w;
            r[ImEarR].z += 0.8f * w;
            break;
        }
        case AiState::Spawning:
        case AiState::Dying:
            r[ImPelvis].x = 0.5f;
            r[ImArmL].x = r[ImArmR].x = -1.5f;
            break;
    }
}

void animateShroom(Enemy& e, float time, Vec3* r, Vec3& root) {
    float s = std::sin(e.phase);
    float t = e.stateTime;
    r[ShCap].z = 0.05f * std::sin(time * 1.7f + e.seed);
    r[ShArmL].z = 0.4f;
    r[ShArmR].z = -0.4f;
    switch (e.state) {
        case AiState::Chase:
        case AiState::Idle: {
            // Andar de pato: se balancea de lado a lado dando pasitos.
            float run = e.state == AiState::Chase ? 1.0f : 0.4f;
            r[ShRoot].z = 0.18f * s * run;
            r[ShCap].z -= 0.12f * s * run;
            r[ShLegL].x = -0.6f * s * run;
            r[ShLegR].x = 0.6f * s * run;
            r[ShArmL].x = 0.5f * s * run;
            r[ShArmR].x = -0.5f * s * run;
            root.y = 0.05f * std::fabs(std::cos(e.phase)) * run;
            break;
        }
        case AiState::Windup: {
            // Se encoge para coger impulso; el sombrero tiembla.
            float w = smoothstep01(t / 0.4f);
            root.y = -0.14f * w;
            r[ShLegL].x = r[ShLegR].x = -0.9f * w;
            r[ShFootL].x = r[ShFootR].x = 0.9f * w;
            r[ShArmL].z = r[ShArmR].z = 0;
            r[ShArmL].x = r[ShArmR].x = 0.8f * w;
            r[ShCap].x = 0.15f * std::sin(time * 50.0f) * w;
            r[ShStem].x = 0.2f * w;
            break;
        }
        case AiState::Attack: {
            // En el aire: estirada, brazos arriba, sombrero hacia atrás.
            r[ShArmL].x = r[ShArmR].x = -2.8f;
            r[ShLegL].x = r[ShLegR].x = 0.2f;
            r[ShFootL].x = r[ShFootR].x = 0.5f;
            r[ShCap].x = -0.25f;
            break;
        }
        case AiState::Recover: {
            // Aplastada tras el golpe contra el suelo.
            float w = 1.0f - smoothstep01(t / 0.6f);
            root.y = -0.12f * w;
            r[ShLegL].z = 0.5f * w;
            r[ShLegR].z = -0.5f * w;
            r[ShCap].x = 0.2f * w;
            break;
        }
        case AiState::Stagger: {
            float w = std::sin(clampf(t / 0.45f, 0, 1) * kPi);
            r[ShStem].x = -0.4f * w;
            r[ShCap].x = -0.3f * w;
            r[ShArmL].z = 1.2f * w;
            r[ShArmR].z = -1.2f * w;
            break;
        }
        case AiState::Spawning:
        case AiState::Dying: break;
    }
}

void animateMoth(Enemy& e, float time, Vec3* r, Vec3& root) {
    // Aleteo continuo (más rápido al preparar el disparo) y vaivén vertical.
    float flap = std::sin(e.phase);
    float amp = e.state == AiState::Windup ? 1.0f : 0.75f;
    r[MoWingL].z = 0.25f + amp * flap;
    r[MoWingR].z = -0.25f - amp * flap;
    r[MoWingL2].z = 0.15f + 0.8f * amp * std::sin(e.phase - 0.5f);
    r[MoWingR2].z = -0.15f - 0.8f * amp * std::sin(e.phase - 0.5f);
    r[MoAntL].z = -0.3f + 0.1f * std::sin(time * 3.0f + e.seed);
    r[MoAntR].z = 0.3f - 0.1f * std::sin(time * 3.3f + e.seed);
    r[MoAntL].x = r[MoAntR].x = 0.4f;
    r[MoAbdomen].x = 0.2f + 0.1f * std::sin(time * 2.0f + e.seed);
    root.y = 0.08f * std::sin(time * 2.5f + e.seed);
    float t = e.stateTime;
    switch (e.state) {
        case AiState::Chase: r[MoThorax].x = 0.3f; break;  // inclinada hacia delante al volar
        case AiState::Windup:
            r[MoThorax].x = -0.4f * smoothstep01(t / 0.3f);  // se echa atrás para disparar
            r[MoAntL].x = r[MoAntR].x = -0.2f;
            break;
        case AiState::Attack: r[MoThorax].x = 0.5f; break;
        case AiState::Stagger: {
            float w = std::sin(clampf(t / 0.45f, 0, 1) * kPi);
            r[MoThorax].z = 0.8f * w;
            r[MoThorax].x = -0.5f * w;
            break;
        }
        default: break;
    }
}

}  // namespace

const char* aiStateName(AiState s) {
    switch (s) {
        case AiState::Spawning: return "APARECE";
        case AiState::Idle: return "VAGA";
        case AiState::Chase: return "PERSIGUE";
        case AiState::Windup: return "PREPARA";
        case AiState::Attack: return "ATACA";
        case AiState::Recover: return "RECUPERA";
        case AiState::Stagger: return "TAMBALEA";
        case AiState::Dying: return "MUERE";
    }
    return "?";
}

const EnemyDef& enemyDef(EnemyKind k) { return kDefs[int(k)]; }

Enemy makeEnemy(EnemyKind kind, Vec3 pos, float yaw, float seed) {
    Enemy e;
    e.kind = kind;
    const EnemyDef& d = e.def();
    e.body.pos = pos;
    e.body.radius = d.radius;
    e.body.height = d.height;
    e.yaw = yaw;
    e.health = d.maxHealth;
    e.seed = seed;
    e.phase = seed;
    animateEnemy(e, 0, 0);
    for (int i = 0; i < d.jointCount; ++i) e.prevJoints[i] = e.joints[i];
    e.prevPos = pos;
    return e;
}

Vec3 Enemy::center() const { return body.pos + Vec3{0, def().height * 0.5f, 0}; }

Mat4 Enemy::modelMatrix() const {
    Mat4 m = translate(body.pos) * rotateY(yaw);
    if (state == AiState::Spawning) {  // crece desde el portal del suelo
        float s = smoothstep01(stateTime / 0.8f);
        return m * scale({s, s, s});
    }
    if (state == AiState::Dying) {  // gira y se encoge hasta desaparecer en luz
        float u = smoothstep01(stateTime / 0.5f);
        float s = 1.0f - 0.85f * u;
        return m * translate({0, def().height * 0.5f, 0}) * rotateY(u * 6.0f) * scale({s, s, s}) *
               translate({0, -def().height * 0.5f, 0});
    }
    return m;
}

void animateEnemy(Enemy& e, float time, float dt) {
    const EnemyDef& d = e.def();
    // Inercia de los apéndices: un punto con muelle sigue al centro del cuerpo; su retraso
    // (en el espacio del enemigo) inclina orejas, cola, sombrero y antenas.
    Vec3 c = e.center();
    if (!e.lagInit) {
        e.lagInit = true;
        e.lagPoint = c;
    }
    Vec3 acc = (c - e.lagPoint) * 120.0f - e.lagVel * 11.0f;
    e.lagVel += acc * dt;
    e.lagPoint += e.lagVel * dt;
    Vec3 lag = e.lagPoint - c;
    float fwd = clampf(dot(lag, e.forward()) * 5.0f, -0.6f, 0.6f);  // > 0: el punto va por delante
    float side = clampf(dot(lag, Vec3{std::cos(e.yaw), 0, -std::sin(e.yaw)}) * 5.0f, -0.6f, 0.6f);
    float up = clampf(lag.y * 5.0f, -0.6f, 0.6f);

    Pose target;
    Vec3* r = target.rotation;
    Vec3& root = target.rootOffset;
    switch (e.kind) {
        case EnemyKind::Imp:
            animateImp(e, time, r, root);
            r[ImEarL].x -= fwd + up;
            r[ImEarR].x -= fwd + up;
            r[ImEarL].z += side;
            r[ImEarR].z += side;
            r[ImTail].x -= up * 1.5f;
            r[ImTail].y += side * 1.5f;
            break;
        case EnemyKind::Shroom:
            animateShroom(e, time, r, root);
            r[ShCap].x -= 0.6f * (fwd + 0.5f * up);
            r[ShCap].z += 0.6f * side;
            break;
        case EnemyKind::Moth:
            animateMoth(e, time, r, root);
            r[MoAntL].x -= fwd;
            r[MoAntR].x -= fwd;
            r[MoAbdomen].x -= up;
            break;
        case EnemyKind::Count: break;
    }
    // Fundido al cambiar de estado.
    if (e.state != e.lastState) {
        e.blendFrom = e.pose;
        e.blendTime = 0;
        e.lastState = e.state;
    }
    e.blendTime += dt;
    const float dur = 0.12f;
    e.pose = e.blendTime >= dur ? target : lerpPose(e.blendFrom, target, smoothstep01(e.blendTime / dur), d.jointCount);
    computeWorldMatrices(d.rig, d.jointCount, e.pose, e.modelMatrix(), e.joints);
}
