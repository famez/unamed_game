#include "actors/enemy.h"

namespace {

constexpr uint32_t rgb(uint8_t r, uint8_t g, uint8_t b) {
    return uint32_t(r) | uint32_t(g) << 8 | uint32_t(b) << 16 | 0xFF000000u;
}

// Dos familias, ninguna «mala»: los Deslumbrados (luz) no pueden dejar de recordar; los
// Vaciados (sombra) lo olvidaron todo. Todos fueron personas.
const uint32_t kLightGlow = rgb(255, 232, 180), kLightHot = rgb(255, 250, 235);
const uint32_t kShadowGlow = rgb(150, 80, 255), kShadowEye = rgb(200, 130, 255);

// --- Deslumbrado: figura humana alta y flaca, sobreexpuesta, sin cara (una grieta de luz).
// Encorvado, los brazos le cuelgan hasta las rodillas; se lanza con dos zarpazos seguidos.
enum DazzledJoint { DzPelvis, DzSpine, DzChest, DzHead, DzArmL, DzForeL, DzClawL, DzArmR, DzForeR, DzClawR,
                    DzThighL, DzShinL, DzFootL, DzThighR, DzShinR, DzFootR, DzCount };
// clang-format off
const JointDef kDazzledRig[DzCount] = {
//   nombre     padre     offset                    centro caja           semiejes caja
    {"pelvis",  -1,       {0.00f, 0.95f, 0.00f},    {0, 0, 0},            {0.13f, 0.09f, 0.09f}},
    {"spine",   DzPelvis, {0.00f, 0.10f, 0.00f},    {0, 0.08f, 0},        {0.10f, 0.12f, 0.08f}},
    {"chest",   DzSpine,  {0.00f, 0.24f, 0.00f},    {0, 0.14f, 0},        {0.17f, 0.16f, 0.10f}},
    {"head",    DzChest,  {0.00f, 0.36f, 0.04f},    {0, 0.13f, 0.02f},    {0.11f, 0.16f, 0.12f}},
    {"arm_l",   DzChest,  {0.19f, 0.24f, 0.00f},    {0, -0.17f, 0},       {0.045f, 0.18f, 0.045f}},
    {"fore_l",  DzArmL,   {0.00f, -0.36f, 0.00f},   {0, -0.17f, 0},       {0.04f, 0.18f, 0.04f}},
    {"claw_l",  DzForeL,  {0.00f, -0.36f, 0.00f},   {0, -0.12f, 0.02f},   {0.04f, 0.13f, 0.03f}},
    {"arm_r",   DzChest,  {-0.19f, 0.24f, 0.00f},   {0, -0.17f, 0},       {0.045f, 0.18f, 0.045f}},
    {"fore_r",  DzArmR,   {0.00f, -0.36f, 0.00f},   {0, -0.17f, 0},       {0.04f, 0.18f, 0.04f}},
    {"claw_r",  DzForeR,  {0.00f, -0.36f, 0.00f},   {0, -0.12f, 0.02f},   {0.04f, 0.13f, 0.03f}},
    {"thigh_l", DzPelvis, {0.10f, -0.05f, 0.00f},   {0, -0.22f, 0},       {0.055f, 0.23f, 0.055f}},
    {"shin_l",  DzThighL, {0.00f, -0.45f, 0.00f},   {0, -0.21f, 0},       {0.045f, 0.22f, 0.045f}},
    {"foot_l",  DzShinL,  {0.00f, -0.42f, 0.00f},   {0, -0.02f, 0.06f},   {0.05f, 0.035f, 0.11f}},
    {"thigh_r", DzPelvis, {-0.10f, -0.05f, 0.00f},  {0, -0.22f, 0},       {0.055f, 0.23f, 0.055f}},
    {"shin_r",  DzThighR, {0.00f, -0.45f, 0.00f},   {0, -0.21f, 0},       {0.045f, 0.22f, 0.045f}},
    {"foot_r",  DzShinR,  {0.00f, -0.42f, 0.00f},   {0, -0.02f, 0.06f},   {0.05f, 0.035f, 0.11f}},
};
const uint32_t kDzSkin = rgb(168, 162, 152), kDzDark = rgb(104, 100, 94);
const EnemyShape kDazzledShapes[] = {
    {DzPelvis, {0, 0, 0},              {0.13f, 0.09f, 0.09f},  kDzSkin, false},
    {DzSpine,  {0, 0.08f, -0.01f},     {0.095f, 0.13f, 0.075f}, kDzSkin, false},
    {DzChest,  {0, 0.14f, 0},          {0.17f, 0.16f, 0.10f},  kDzSkin, false},
    {DzChest,  {0, 0.06f, 0.06f},      {0.12f, 0.06f, 0.06f},  kDzDark, false},       // costillas hundidas
    {DzChest,  {0, 0.13f, 0.095f},     {0.018f, 0.11f, 0.012f}, kLightHot, true},     // grieta del pecho
    {DzChest,  {0.07f, 0.2f, 0.085f},  {0.012f, 0.05f, 0.01f}, kLightGlow, true},
    {DzHead,   {0, 0.13f, 0.02f},      {0.105f, 0.165f, 0.12f}, kDzSkin, false},      // cabeza alargada, sin cara
    {DzHead,   {0, 0.13f, 0.13f},      {0.012f, 0.10f, 0.012f}, kLightHot, true},     // la grieta donde iría la cara
    {DzHead,   {0, 0.24f, 0.10f},      {0.05f, 0.012f, 0.012f}, kLightGlow, true},
    {DzArmL,   {0, -0.17f, 0},         {0.045f, 0.19f, 0.045f}, kDzSkin, false},
    {DzForeL,  {0, -0.17f, 0},         {0.038f, 0.19f, 0.038f}, kDzSkin, false},
    {DzClawL,  {0.025f, -0.13f, 0.02f}, {0.012f, 0.14f, 0.014f}, kLightHot, true},   // dedos como cuchillas
    {DzClawL,  {-0.02f, -0.12f, 0.03f}, {0.012f, 0.12f, 0.014f}, kLightHot, true},
    {DzClawL,  {0, -0.02f, 0.01f},     {0.035f, 0.04f, 0.03f}, kDzDark, false},
    {DzArmR,   {0, -0.17f, 0},         {0.045f, 0.19f, 0.045f}, kDzSkin, false},
    {DzForeR,  {0, -0.17f, 0},         {0.038f, 0.19f, 0.038f}, kDzSkin, false},
    {DzClawR,  {-0.025f, -0.13f, 0.02f}, {0.012f, 0.14f, 0.014f}, kLightHot, true},
    {DzClawR,  {0.02f, -0.12f, 0.03f}, {0.012f, 0.12f, 0.014f}, kLightHot, true},
    {DzClawR,  {0, -0.02f, 0.01f},     {0.035f, 0.04f, 0.03f}, kDzDark, false},
    {DzThighL, {0, -0.22f, 0},         {0.055f, 0.24f, 0.055f}, kDzSkin, false},
    {DzShinL,  {0, -0.21f, 0},         {0.042f, 0.22f, 0.042f}, kDzSkin, false},
    {DzFootL,  {0, -0.02f, 0.06f},     {0.045f, 0.035f, 0.11f}, kDzDark, false},
    {DzThighR, {0, -0.22f, 0},         {0.055f, 0.24f, 0.055f}, kDzSkin, false},
    {DzShinR,  {0, -0.21f, 0},         {0.042f, 0.22f, 0.042f}, kDzSkin, false},
    {DzFootR,  {0, -0.02f, 0.06f},     {0.045f, 0.035f, 0.11f}, kDzDark, false},
};

// --- Coloso (sombra): mole encorvada de brazos larguísimos, cabeza pequeña hundida entre
// los hombros y un hueco oscuro en el pecho. Salta y cae aplastando (onda alrededor).
enum BruteJoint { BrPelvis, BrSpine, BrChest, BrHead, BrArmL, BrForeL, BrFistL, BrArmR, BrForeR, BrFistR,
                  BrThighL, BrShinL, BrFootL, BrThighR, BrShinR, BrFootR, BrCount };
const JointDef kBruteRig[BrCount] = {
    {"pelvis",  -1,       {0.00f, 0.92f, 0.00f},    {0, 0, 0},            {0.26f, 0.16f, 0.2f}},
    {"spine",   BrPelvis, {0.00f, 0.14f, 0.00f},    {0, 0.15f, 0},        {0.3f, 0.2f, 0.24f}},
    {"chest",   BrSpine,  {0.00f, 0.32f, 0.04f},    {0, 0.18f, 0},        {0.48f, 0.3f, 0.34f}},
    {"head",    BrChest,  {0.00f, 0.22f, 0.30f},    {0, 0.06f, 0.06f},    {0.14f, 0.13f, 0.15f}},
    {"arm_l",   BrChest,  {0.46f, 0.22f, 0.00f},    {0, -0.24f, 0},       {0.13f, 0.26f, 0.13f}},
    {"fore_l",  BrArmL,   {0.00f, -0.50f, 0.00f},   {0, -0.24f, 0},       {0.12f, 0.26f, 0.12f}},
    {"fist_l",  BrForeL,  {0.00f, -0.50f, 0.00f},   {0, -0.08f, 0},       {0.16f, 0.14f, 0.16f}},
    {"arm_r",   BrChest,  {-0.46f, 0.22f, 0.00f},   {0, -0.24f, 0},       {0.13f, 0.26f, 0.13f}},
    {"fore_r",  BrArmR,   {0.00f, -0.50f, 0.00f},   {0, -0.24f, 0},       {0.12f, 0.26f, 0.12f}},
    {"fist_r",  BrForeR,  {0.00f, -0.50f, 0.00f},   {0, -0.08f, 0},       {0.16f, 0.14f, 0.16f}},
    {"thigh_l", BrPelvis, {0.18f, -0.06f, 0.00f},   {0, -0.2f, 0},        {0.12f, 0.22f, 0.12f}},
    {"shin_l",  BrThighL, {0.00f, -0.42f, 0.00f},   {0, -0.19f, 0},       {0.1f, 0.2f, 0.1f}},
    {"foot_l",  BrShinL,  {0.00f, -0.40f, 0.00f},   {0, -0.03f, 0.07f},   {0.12f, 0.05f, 0.16f}},
    {"thigh_r", BrPelvis, {-0.18f, -0.06f, 0.00f},  {0, -0.2f, 0},        {0.12f, 0.22f, 0.12f}},
    {"shin_r",  BrThighR, {0.00f, -0.42f, 0.00f},   {0, -0.19f, 0},       {0.1f, 0.2f, 0.1f}},
    {"foot_r",  BrShinR,  {0.00f, -0.40f, 0.00f},   {0, -0.03f, 0.07f},   {0.12f, 0.05f, 0.16f}},
};
const uint32_t kBrSkin = rgb(52, 50, 62), kBrDark = rgb(26, 25, 32), kBrStone = rgb(78, 76, 90);
const EnemyShape kBruteShapes[] = {
    {BrPelvis, {0, 0, 0},              {0.26f, 0.17f, 0.21f},  kBrSkin, false},
    {BrSpine,  {0, 0.15f, -0.02f},     {0.3f, 0.22f, 0.25f},   kBrSkin, false},
    {BrChest,  {0, 0.18f, -0.04f},     {0.5f, 0.32f, 0.36f},   kBrSkin, false},
    {BrChest,  {0, 0.38f, -0.12f},     {0.36f, 0.16f, 0.26f},  kBrStone, false},     // joroba
    {BrChest,  {0.3f, 0.36f, -0.05f},  {0.18f, 0.14f, 0.2f},   kBrStone, false},     // hombros como roca
    {BrChest,  {-0.3f, 0.36f, -0.05f}, {0.18f, 0.14f, 0.2f},   kBrStone, false},
    {BrChest,  {0, 0.14f, 0.3f},       {0.13f, 0.15f, 0.06f},  kBrDark, false},      // el hueco del pecho
    {BrChest,  {0, 0.14f, 0.34f},      {0.06f, 0.07f, 0.02f},  kShadowGlow, true},
    {BrChest,  {0.2f, 0.05f, 0.3f},    {0.015f, 0.12f, 0.012f}, kShadowGlow, true},  // grietas
    {BrChest,  {-0.24f, 0.22f, 0.3f},  {0.015f, 0.09f, 0.012f}, kShadowGlow, true},
    {BrHead,   {0, 0.06f, 0.06f},      {0.14f, 0.13f, 0.15f},  kBrSkin, false},
    {BrHead,   {0.05f, 0.07f, 0.19f},  {0.025f, 0.01f, 0.01f}, kShadowEye, true},
    {BrHead,   {-0.05f, 0.07f, 0.19f}, {0.025f, 0.01f, 0.01f}, kShadowEye, true},
    {BrArmL,   {0, -0.24f, 0},         {0.14f, 0.28f, 0.14f},  kBrSkin, false},
    {BrForeL,  {0, -0.24f, 0},         {0.12f, 0.28f, 0.12f},  kBrSkin, false},
    {BrFistL,  {0, -0.08f, 0.02f},     {0.17f, 0.15f, 0.17f},  kBrStone, false},
    {BrArmR,   {0, -0.24f, 0},         {0.14f, 0.28f, 0.14f},  kBrSkin, false},
    {BrForeR,  {0, -0.24f, 0},         {0.12f, 0.28f, 0.12f},  kBrSkin, false},
    {BrFistR,  {0, -0.08f, 0.02f},     {0.17f, 0.15f, 0.17f},  kBrStone, false},
    {BrThighL, {0, -0.2f, 0},          {0.13f, 0.23f, 0.13f},  kBrSkin, false},
    {BrShinL,  {0, -0.19f, 0},         {0.1f, 0.21f, 0.1f},    kBrSkin, false},
    {BrFootL,  {0, -0.03f, 0.07f},     {0.12f, 0.06f, 0.17f},  kBrDark, false},
    {BrThighR, {0, -0.2f, 0},          {0.13f, 0.23f, 0.13f},  kBrSkin, false},
    {BrShinR,  {0, -0.19f, 0},         {0.1f, 0.21f, 0.1f},    kBrSkin, false},
    {BrFootR,  {0, -0.03f, 0.07f},     {0.12f, 0.06f, 0.17f},  kBrDark, false},
};

// --- Vigía (luz): un ojo enorme que flota dentro de dos anillos de esquirlas que giran,
// con jirones de tela colgando. Dispara luz desde lejos.
enum WatcherJoint { WaCore, WaEye, WaRingA, WaRingB, WaStrip1, WaStrip2, WaStrip3, WaStrip4, WaCount };
const JointDef kWatcherRig[WaCount] = {
    {"core",    -1,     {0.00f, 0.30f, 0.00f},     {0, 0, 0},             {0.18f, 0.18f, 0.18f}},
    {"eye",     WaCore, {0.00f, 0.00f, 0.08f},     {0, 0, 0.05f},         {0.1f, 0.1f, 0.06f}},
    {"ring_a",  WaCore, {0.00f, 0.00f, 0.00f},     {0, 0, 0},             {0.38f, 0.02f, 0.38f}},
    {"ring_b",  WaCore, {0.00f, 0.00f, 0.00f},     {0, 0, 0},             {0.30f, 0.02f, 0.30f}},
    {"strip1",  WaCore, {0.08f, -0.14f, -0.04f},   {0, -0.22f, 0},        {0.03f, 0.22f, 0.01f}},
    {"strip2",  WaCore, {-0.08f, -0.14f, -0.04f},  {0, -0.22f, 0},        {0.03f, 0.22f, 0.01f}},
    {"strip3",  WaCore, {0.03f, -0.15f, -0.1f},    {0, -0.3f, 0},         {0.03f, 0.3f, 0.01f}},
    {"strip4",  WaCore, {-0.04f, -0.15f, 0.06f},   {0, -0.18f, 0},        {0.03f, 0.18f, 0.01f}},
};
const uint32_t kWaShell = rgb(200, 196, 186), kWaCloth = rgb(170, 166, 156);
const EnemyShape kWatcherShapes[] = {
    {WaCore,   {0, 0, -0.03f},         {0.19f, 0.19f, 0.17f},  kWaShell, false},
    {WaCore,   {0, 0.02f, -0.12f},     {0.12f, 0.13f, 0.1f},   kWaCloth, false},
    {WaEye,    {0, 0, 0.04f},          {0.11f, 0.11f, 0.07f},  kLightHot, true},
    {WaEye,    {0, 0, 0.1f},           {0.035f, 0.05f, 0.02f}, rgb(30, 28, 26), false},  // pupila
    {WaRingA,  {0.36f, 0, 0},          {0.045f, 0.03f, 0.045f}, kLightGlow, true},
    {WaRingA,  {-0.36f, 0, 0},         {0.045f, 0.03f, 0.045f}, kLightGlow, true},
    {WaRingA,  {0, 0, 0.36f},          {0.045f, 0.03f, 0.045f}, kLightGlow, true},
    {WaRingA,  {0, 0, -0.36f},         {0.045f, 0.03f, 0.045f}, kLightGlow, true},
    {WaRingA,  {0.255f, 0, 0.255f},    {0.03f, 0.02f, 0.03f},  kLightHot, true},
    {WaRingA,  {-0.255f, 0, 0.255f},   {0.03f, 0.02f, 0.03f},  kLightHot, true},
    {WaRingA,  {0.255f, 0, -0.255f},   {0.03f, 0.02f, 0.03f},  kLightHot, true},
    {WaRingA,  {-0.255f, 0, -0.255f},  {0.03f, 0.02f, 0.03f},  kLightHot, true},
    {WaRingB,  {0.29f, 0, 0},          {0.035f, 0.035f, 0.02f}, kLightGlow, true},
    {WaRingB,  {-0.29f, 0, 0},         {0.035f, 0.035f, 0.02f}, kLightGlow, true},
    {WaRingB,  {0, 0.29f, 0},          {0.035f, 0.035f, 0.02f}, kLightGlow, true},
    {WaRingB,  {0, -0.29f, 0},         {0.035f, 0.035f, 0.02f}, kLightGlow, true},
    {WaStrip1, {0, -0.22f, 0},         {0.035f, 0.23f, 0.012f}, kWaCloth, false},
    {WaStrip2, {0, -0.22f, 0},         {0.035f, 0.23f, 0.012f}, kWaCloth, false},
    {WaStrip3, {0, -0.3f, 0},          {0.03f, 0.31f, 0.012f}, kWaCloth, false},
    {WaStrip4, {0, -0.18f, 0},         {0.03f, 0.19f, 0.012f}, kWaCloth, false},
};

// --- Sabueso (sombra): cuadrúpedo esquelético, casi negro, de patas largas y cabeza baja;
// caza en manada, rodea y muerde por la espalda.
enum HoundJoint { HdPelvis, HdChest, HdNeck, HdHead, HdJaw, HdFUpL, HdFLowL, HdFPawL, HdFUpR, HdFLowR, HdFPawR,
                  HdHUpL, HdHLowL, HdHPawL, HdHUpR, HdHLowR, HdHPawR, HdTail, HdCount };
const JointDef kHoundRig[HdCount] = {
    {"pelvis",  -1,       {0.00f, 0.68f, -0.30f},   {0, 0, 0},            {0.14f, 0.13f, 0.17f}},
    {"chest",   HdPelvis, {0.00f, 0.06f, 0.60f},    {0, 0, 0},            {0.17f, 0.19f, 0.19f}},
    {"neck",    HdChest,  {0.00f, 0.08f, 0.17f},    {0, 0.03f, 0.08f},    {0.08f, 0.08f, 0.13f}},
    {"head",    HdNeck,   {0.00f, 0.03f, 0.19f},    {0, 0.01f, 0.12f},    {0.1f, 0.09f, 0.18f}},
    {"jaw",     HdHead,   {0.00f, -0.05f, 0.05f},   {0, -0.02f, 0.12f},   {0.05f, 0.025f, 0.12f}},
    {"fup_l",   HdChest,  {0.13f, -0.08f, 0.04f},   {0, -0.15f, 0},       {0.05f, 0.16f, 0.055f}},
    {"flow_l",  HdFUpL,   {0.00f, -0.30f, 0.00f},   {0, -0.15f, 0},       {0.035f, 0.16f, 0.035f}},
    {"fpaw_l",  HdFLowL,  {0.00f, -0.30f, 0.00f},   {0, -0.01f, 0.04f},   {0.045f, 0.03f, 0.07f}},
    {"fup_r",   HdChest,  {-0.13f, -0.08f, 0.04f},  {0, -0.15f, 0},       {0.05f, 0.16f, 0.055f}},
    {"flow_r",  HdFUpR,   {0.00f, -0.30f, 0.00f},   {0, -0.15f, 0},       {0.035f, 0.16f, 0.035f}},
    {"fpaw_r",  HdFLowR,  {0.00f, -0.30f, 0.00f},   {0, -0.01f, 0.04f},   {0.045f, 0.03f, 0.07f}},
    {"hup_l",   HdPelvis, {0.12f, -0.02f, 0.00f},   {0, -0.15f, 0},       {0.06f, 0.16f, 0.07f}},
    {"hlow_l",  HdHUpL,   {0.00f, -0.30f, 0.00f},   {0, -0.16f, 0},       {0.035f, 0.17f, 0.035f}},
    {"hpaw_l",  HdHLowL,  {0.00f, -0.32f, 0.00f},   {0, -0.01f, 0.04f},   {0.045f, 0.03f, 0.07f}},
    {"hup_r",   HdPelvis, {-0.12f, -0.02f, 0.00f},  {0, -0.15f, 0},       {0.06f, 0.16f, 0.07f}},
    {"hlow_r",  HdHUpR,   {0.00f, -0.30f, 0.00f},   {0, -0.16f, 0},       {0.035f, 0.17f, 0.035f}},
    {"hpaw_r",  HdHLowR,  {0.00f, -0.32f, 0.00f},   {0, -0.01f, 0.04f},   {0.045f, 0.03f, 0.07f}},
    {"tail",    HdPelvis, {0.00f, 0.05f, -0.15f},   {0, 0, -0.2f},        {0.03f, 0.03f, 0.2f}},
};
const uint32_t kHdSkin = rgb(34, 32, 42), kHdRib = rgb(64, 60, 76), kHdTooth = rgb(190, 186, 196);
const EnemyShape kHoundShapes[] = {
    {HdPelvis, {0, 0, 0},              {0.14f, 0.13f, 0.17f},  kHdSkin, false},
    {HdPelvis, {0, 0.02f, 0.3f},       {0.12f, 0.12f, 0.28f},  kHdSkin, false},      // vientre hundido
    {HdPelvis, {0, 0.13f, 0.25f},      {0.016f, 0.02f, 0.3f},  kShadowGlow, true},   // espinazo
    {HdChest,  {0, 0, 0},              {0.17f, 0.19f, 0.2f},   kHdSkin, false},
    {HdChest,  {0.12f, -0.03f, 0},     {0.05f, 0.13f, 0.15f},  kHdRib, false},       // costillas
    {HdChest,  {-0.12f, -0.03f, 0},    {0.05f, 0.13f, 0.15f},  kHdRib, false},
    {HdChest,  {0, 0.17f, -0.02f},     {0.016f, 0.03f, 0.16f}, kShadowGlow, true},
    {HdNeck,   {0, 0.03f, 0.08f},      {0.08f, 0.085f, 0.14f}, kHdSkin, false},
    {HdHead,   {0, 0.01f, 0.08f},      {0.1f, 0.085f, 0.12f},  kHdSkin, false},
    {HdHead,   {0, -0.01f, 0.22f},     {0.055f, 0.05f, 0.11f}, kHdSkin, false},      // hocico largo
    {HdHead,   {0.055f, 0.04f, 0.14f}, {0.026f, 0.01f, 0.012f}, kShadowEye, true},   // ojos rasgados
    {HdHead,   {-0.055f, 0.04f, 0.14f}, {0.026f, 0.01f, 0.012f}, kShadowEye, true},
    {HdHead,   {0.06f, 0.1f, 0.0f},    {0.02f, 0.07f, 0.03f},  kHdSkin, false},      // orejas hacia atrás
    {HdHead,   {-0.06f, 0.1f, 0.0f},   {0.02f, 0.07f, 0.03f},  kHdSkin, false},
    {HdJaw,    {0, -0.02f, 0.12f},     {0.05f, 0.026f, 0.12f}, kHdSkin, false},
    {HdJaw,    {0.03f, 0.015f, 0.17f}, {0.008f, 0.02f, 0.06f}, kHdTooth, false},     // dientes
    {HdJaw,    {-0.03f, 0.015f, 0.17f}, {0.008f, 0.02f, 0.06f}, kHdTooth, false},
    {HdJaw,    {0, 0.0f, 0.1f},        {0.03f, 0.012f, 0.06f}, kShadowGlow, true},   // fauces
    {HdFUpL,   {0, -0.15f, 0},         {0.05f, 0.17f, 0.055f}, kHdSkin, false},
    {HdFLowL,  {0, -0.15f, 0},         {0.032f, 0.17f, 0.032f}, kHdSkin, false},
    {HdFPawL,  {0, -0.01f, 0.04f},     {0.045f, 0.03f, 0.075f}, kHdRib, false},
    {HdFUpR,   {0, -0.15f, 0},         {0.05f, 0.17f, 0.055f}, kHdSkin, false},
    {HdFLowR,  {0, -0.15f, 0},         {0.032f, 0.17f, 0.032f}, kHdSkin, false},
    {HdFPawR,  {0, -0.01f, 0.04f},     {0.045f, 0.03f, 0.075f}, kHdRib, false},
    {HdHUpL,   {0, -0.15f, 0},         {0.06f, 0.17f, 0.075f}, kHdSkin, false},
    {HdHLowL,  {0, -0.16f, 0},         {0.032f, 0.18f, 0.032f}, kHdSkin, false},
    {HdHPawL,  {0, -0.01f, 0.04f},     {0.045f, 0.03f, 0.075f}, kHdRib, false},
    {HdHUpR,   {0, -0.15f, 0},         {0.06f, 0.17f, 0.075f}, kHdSkin, false},
    {HdHLowR,  {0, -0.16f, 0},         {0.032f, 0.18f, 0.032f}, kHdSkin, false},
    {HdHPawR,  {0, -0.01f, 0.04f},     {0.045f, 0.03f, 0.075f}, kHdRib, false},
    {HdTail,   {0, 0, -0.2f},          {0.03f, 0.03f, 0.21f},  kHdSkin, false},
};

// --- Polilla de tinta (sombra): un triángulo negro con un punto violeta. Llegan en
// enjambre a las farolas para apagarlas. Mismos huesos que polilla.glb (sin rotación).
enum MothJoint { MoRoot, MoBody, MoHead, MoWingL, MoWingR, MoCount };
const JointDef kMothRig[MoCount] = {
    {"root",   -1,     {0, 0, 0},           {0, 0, 0}, {0, 0, 0}},
    {"body",   MoRoot, {0, 0.10f, 0},       {0, 0, 0}, {0.03f, 0.03f, 0.08f}},
    {"head",   MoBody, {0, 0, 0.07f},       {0, 0, 0}, {0.03f, 0.03f, 0.03f}},
    {"wing_l", MoBody, {0.03f, 0.02f, 0},   {0, 0, 0}, {0, 0, 0}},
    {"wing_r", MoBody, {-0.03f, 0.02f, 0},  {0, 0, 0}, {0, 0, 0}},
};
const EnemyShape kMothShapes[] = {  // si no está polilla.glb
    {MoBody,  {0, 0, 0},           {0.035f, 0.03f, 0.09f},  rgb(30, 24, 40), false},
    {MoWingL, {0.12f, 0, -0.02f},  {0.12f, 0.008f, 0.08f},  rgb(22, 18, 30), false},
    {MoWingR, {-0.12f, 0, -0.02f}, {0.12f, 0.008f, 0.08f},  rgb(22, 18, 30), false},
    {MoHead,  {0, 0.01f, 0.02f},   {0.015f, 0.015f, 0.015f}, kShadowEye, true},
};

// --- Luciérnaga de cristal (luz): octaedro de luz con alas de filamento. Chispas que se
// escapan de las farolas rotas; explotan al morir y encienden lo que tocan.
enum FireflyJoint { FfRoot, FfCore, FfWingL, FfWingR, FfTail, FfCount };
const JointDef kFireflyRig[FfCount] = {
    {"root",   -1,     {0, 0, 0},           {0, 0, 0}, {0, 0, 0}},
    {"core",   FfRoot, {0, 0.10f, 0},       {0, 0, 0}, {0.04f, 0.04f, 0.04f}},
    {"wing_l", FfCore, {0.02f, 0.03f, 0},   {0, 0, 0}, {0, 0, 0}},
    {"wing_r", FfCore, {-0.02f, 0.03f, 0},  {0, 0, 0}, {0, 0, 0}},
    {"tail",   FfCore, {0, 0, -0.04f},      {0, 0, 0}, {0.02f, 0.02f, 0.03f}},
};
const EnemyShape kFireflyShapes[] = {  // si no está luciernaga.glb
    {FfCore,  {0, 0, 0},            {0.05f, 0.06f, 0.05f},   kLightHot, true},
    {FfWingL, {0.07f, 0, 0},        {0.07f, 0.005f, 0.04f},  kLightGlow, true},
    {FfWingR, {-0.07f, 0, 0},       {0.07f, 0.005f, 0.04f},  kLightGlow, true},
    {FfTail,  {0, 0, -0.03f},       {0.025f, 0.025f, 0.035f}, kLightGlow, true},
};

#define COUNT(a) int(sizeof(a) / sizeof((a)[0]))
const EnemyDef kDefs[int(EnemyKind::Count)] = {
//   nombre        rig          n        formas          n                     vida  vel.  radio alto  aggro alc.  prep. ataque recup. espera daño golpe vuela altura munny sombra
    {"Deslumbrado", kDazzledRig, DzCount, kDazzledShapes, COUNT(kDazzledShapes), 45, 5.0f, 0.35f, 1.9f, 20, 2.5f, 0.32f, 0.55f, 0.45f, 0.7f,  11, 5.0f, false, 0,    3, 6,  false},
    {"Coloso",      kBruteRig,   BrCount, kBruteShapes,   COUNT(kBruteShapes),   110, 2.7f, 0.6f, 2.2f, 16, 3.4f, 0.55f, 1.00f, 0.70f, 1.3f,  22, 9.0f, false, 0,    6, 10, true},
    {"Vigía",       kWatcherRig, WaCount, kWatcherShapes, COUNT(kWatcherShapes), 32, 3.6f, 0.40f, 0.7f, 22, 10.0f, 0.60f, 0.30f, 0.60f, 1.3f, 13, 4.0f, true,  2.6f, 4, 8,  false},
    {"Sabueso",     kHoundRig,   HdCount, kHoundShapes,   COUNT(kHoundShapes),   38, 6.2f, 0.42f, 0.9f, 22, 3.0f, 0.28f, 0.40f, 0.50f, 0.8f,  10, 4.0f, false, 0,    2, 5,  true},
    // Enjambres: mueren de un golpe; el cuerpo de colisión, algo más grande que el bicho (que se le pueda dar).
    {"Polilla de tinta",      kMothRig,    MoCount, kMothShapes,    COUNT(kMothShapes),    1, 4.5f, 0.3f, 0.5f, 18, 1.0f, 0.20f, 0.20f, 0.30f, 1.2f, 2, 1.0f, true, 1.6f, 0, 1, true},
    {"Luciérnaga de cristal", kFireflyRig, FfCount, kFireflyShapes, COUNT(kFireflyShapes), 1, 9.0f, 0.3f, 0.5f, 22, 1.2f, 0.25f, 0.20f, 0.40f, 1.0f, 5, 2.5f, true, 1.3f, 0, 1, false},
};
#undef COUNT
// clang-format on

static_assert(DzCount <= kMaxJoints && BrCount <= kMaxJoints && WaCount <= kMaxJoints && HdCount <= kMaxJoints && MoCount <= kMaxJoints &&
                  FfCount <= kMaxJoints,
              "rig demasiado grande");

// Sacudida nerviosa: casi siempre quieta y, de vez en cuando, un tirón brusco.
float twitch(float time, float seed, float rate) {
    float ph = std::fmod(time * rate + seed * 3.7f, 1.0f);
    return ph < 0.06f ? std::sin(ph / 0.06f * kPi) * (std::fmod(seed * 17.0f + std::floor(time * rate), 2.0f) - 1.0f) : 0.0f;
}

void animateDazzled(Enemy& e, float time, Vec3* r, Vec3& root) {
    float s = std::sin(e.phase);
    float t = e.stateTime;
    // Postura: encorvado, cabeza adelantada y ladeada, brazos colgando. Tirones de cabeza.
    r[DzSpine].x = 0.35f;
    r[DzChest].x = 0.2f;
    r[DzHead].x = -0.25f + 0.6f * twitch(time, e.seed, 0.7f);
    r[DzHead].z = 0.25f * std::sin(time * 0.6f + e.seed) + 0.8f * twitch(time, e.seed + 1.0f, 0.5f);
    r[DzArmL].z = 0.12f;
    r[DzArmR].z = -0.12f;
    r[DzArmL].x = r[DzArmR].x = -0.25f;
    r[DzForeL].x = r[DzForeR].x = -0.2f;
    switch (e.state) {
        case AiState::Chase:
        case AiState::Idle: {
            float run = e.state == AiState::Chase ? 1.0f : 0.35f;
            r[DzThighL].x = -0.75f * s * run;
            r[DzThighR].x = 0.75f * s * run;
            r[DzShinL].x = 0.9f * std::max(0.0f, std::sin(e.phase + 1.2f)) * run;
            r[DzShinR].x = 0.9f * std::max(0.0f, std::sin(e.phase + 1.2f + kPi)) * run;
            r[DzSpine].x += 0.25f * run;  // corre agachado, los brazos atrás como alas rotas
            r[DzArmL].x = 0.5f * run - 0.25f + 0.2f * s * run;
            r[DzArmR].x = 0.5f * run - 0.25f - 0.2f * s * run;
            r[DzArmL].z = 0.12f + 0.3f * run;
            r[DzArmR].z = -0.12f - 0.3f * run;
            root.y = 0.05f * std::fabs(std::cos(e.phase)) * run;
            break;
        }
        case AiState::Windup: {
            // Se yergue, abre los brazos y ladea la cabeza del todo; tiembla.
            float w = smoothstep01(t / 0.25f);
            r[DzSpine].x = lerpf(0.35f, -0.1f, w);
            r[DzArmL].z = lerpf(0.12f, 1.4f, w);
            r[DzArmR].z = lerpf(-0.12f, -1.4f, w);
            r[DzArmL].x = r[DzArmR].x = -0.6f * w;
            r[DzForeL].x = r[DzForeR].x = -1.2f * w;
            r[DzHead].z = 1.1f * w;
            r[DzChest].y = 0.06f * std::sin(time * 70.0f) * w;
            r[DzThighL].x = r[DzThighR].x = -0.4f * w;
            r[DzShinL].x = r[DzShinR].x = 0.8f * w;
            root.y = -0.12f * w;
            break;
        }
        case AiState::Attack: {
            // Dos zarpazos cruzados: primero el derecho, luego el izquierdo.
            float u1 = smoothstep01(clampf(t / 0.2f, 0, 1)), u2 = smoothstep01(clampf((t - 0.25f) / 0.2f, 0, 1));
            r[DzSpine].x = 0.6f;
            r[DzChest].y = 0.5f * u1 - 1.0f * u2;
            r[DzArmR].x = lerpf(-2.4f, -0.8f, u1);
            r[DzArmR].y = lerpf(-0.8f, 0.9f, u1);
            r[DzArmL].x = lerpf(-2.4f, -0.8f, u2);
            r[DzArmL].y = lerpf(0.8f, -0.9f, u2);
            r[DzThighL].x = -0.9f;
            r[DzThighR].x = 0.5f;
            r[DzHead].z = 0.4f;
            break;
        }
        case AiState::Recover:
            r[DzSpine].x = 0.7f;
            r[DzArmL].x = r[DzArmR].x = 0.1f;
            r[DzHead].x = 0.3f;
            break;
        case AiState::Stagger: {
            float w = std::sin(clampf(t / 0.45f, 0, 1) * kPi);
            r[DzSpine].x = 0.35f - 0.7f * w;
            r[DzHead].x = -0.9f * w;
            r[DzArmL].z = 1.0f * w;
            r[DzArmR].z = -1.0f * w;
            break;
        }
        case AiState::Spawning:
        case AiState::Dying:
            r[DzSpine].x = 1.0f;
            r[DzHead].x = 0.8f;
            r[DzArmL].x = r[DzArmR].x = 0.3f;
            break;
    }
}

void animateBrute(Enemy& e, float time, Vec3* r, Vec3& root) {
    float s = std::sin(e.phase);
    float t = e.stateTime;
    r[BrSpine].x = 0.35f;
    r[BrChest].x = 0.15f + 0.03f * std::sin(time * 1.3f + e.seed);  // respiración lenta
    r[BrHead].x = -0.4f;
    r[BrArmL].z = 0.25f;
    r[BrArmR].z = -0.25f;
    r[BrArmL].x = r[BrArmR].x = -0.15f;
    switch (e.state) {
        case AiState::Chase:
        case AiState::Idle: {
            float run = e.state == AiState::Chase ? 1.0f : 0.4f;
            r[BrThighL].x = -0.55f * s * run;
            r[BrThighR].x = 0.55f * s * run;
            r[BrShinL].x = 0.6f * std::max(0.0f, std::sin(e.phase + 1.2f)) * run;
            r[BrShinR].x = 0.6f * std::max(0.0f, std::sin(e.phase + 1.2f + kPi)) * run;
            r[BrPelvis].z = 0.08f * s * run;  // pesado: se balancea de lado
            r[BrArmL].x = -0.15f + 0.45f * s * run;  // los brazos casi arrastran
            r[BrArmR].x = -0.15f - 0.45f * s * run;
            root.y = 0.06f * std::fabs(std::cos(e.phase)) * run;
            break;
        }
        case AiState::Windup: {
            // Levanta los dos puños por encima de la cabeza.
            float w = smoothstep01(t / 0.45f);
            r[BrSpine].x = lerpf(0.35f, -0.25f, w);
            r[BrArmL].x = r[BrArmR].x = -2.9f * w;
            r[BrArmL].z = 0.25f - 0.1f * w;
            r[BrArmR].z = -0.25f + 0.1f * w;
            r[BrForeL].x = r[BrForeR].x = -0.6f * w;
            r[BrThighL].x = r[BrThighR].x = -0.6f * w;
            r[BrShinL].x = r[BrShinR].x = 0.9f * w;
            root.y = -0.15f * w;
            r[BrChest].y = 0.04f * std::sin(time * 40.0f) * w;
            break;
        }
        case AiState::Attack:
            r[BrSpine].x = -0.1f;
            r[BrArmL].x = r[BrArmR].x = -2.9f;
            r[BrThighL].x = r[BrThighR].x = -0.3f;
            break;
        case AiState::Recover: {  // los puños contra el suelo
            float w = 1.0f - smoothstep01(t / 0.7f);
            r[BrSpine].x = 0.35f + 0.6f * w;
            r[BrArmL].x = r[BrArmR].x = -0.15f - 0.9f * w;
            r[BrThighL].x = r[BrThighR].x = -0.6f * w;
            r[BrShinL].x = r[BrShinR].x = 0.9f * w;
            root.y = -0.2f * w;
            break;
        }
        case AiState::Stagger: {
            float w = std::sin(clampf(t / 0.45f, 0, 1) * kPi);
            r[BrSpine].x = 0.35f - 0.5f * w;
            r[BrHead].x = -0.8f * w;
            break;
        }
        case AiState::Spawning:
        case AiState::Dying:
            r[BrSpine].x = 0.9f;
            r[BrArmL].x = r[BrArmR].x = -0.6f;
            break;
    }
}

void animateWatcher(Enemy& e, float time, Vec3* r, Vec3& root) {
    float spin = e.state == AiState::Windup ? 7.0f : 1.4f;
    r[WaRingA].y = e.phase * spin * 0.1f;
    r[WaRingA].x = 0.3f;
    r[WaRingB].z = -e.phase * spin * 0.13f;
    r[WaRingB].y = 1.2f;
    for (int k = 0; k < 4; ++k) {  // jirones que ondean
        r[WaStrip1 + k].x = 0.2f + 0.25f * std::sin(time * 2.3f + float(k) * 1.3f + e.seed);
        r[WaStrip1 + k].z = 0.15f * std::sin(time * 1.7f + float(k) * 2.1f + e.seed);
    }
    root.y = 0.1f * std::sin(time * 1.2f + e.seed);
    // El ojo mira a todas partes en reposo; fijo al apuntar.
    r[WaEye].y = 0.35f * std::sin(time * 0.9f + e.seed) * (e.state == AiState::Idle ? 1.0f : 0.15f);
    r[WaEye].x = 0.2f * twitch(time, e.seed, 0.8f);
    float t = e.stateTime;
    switch (e.state) {
        case AiState::Chase: r[WaCore].x = 0.25f; break;
        case AiState::Windup: r[WaCore].x = -0.3f * smoothstep01(t / 0.3f); break;
        case AiState::Attack: r[WaCore].x = 0.5f; break;
        case AiState::Stagger: {
            float w = std::sin(clampf(t / 0.45f, 0, 1) * kPi);
            r[WaCore].z = 0.9f * w;
            r[WaCore].x = -0.6f * w;
            break;
        }
        default: break;
    }
}

// El sabueso se anima en dos capas: el cuerpo (cadera, pecho, cuello, cabeza, mandíbula y cola)
// según el estado, y las patas, que no tienen animación propia: cada pata busca un punto del
// suelo (IK de dos huesos) que avanza con el ciclo de pasos. Así no patinan, se doblan solas al
// agazaparse y el paso cambia con la velocidad: al paso (las cuatro por turnos), al trote (en
// diagonal) y al galope (las traseras empujan juntas y la espalda se encoge y se estira).
struct HoundGait {
    float freq;     // ciclos por segundo
    float duty;     // fracción del ciclo con la pata en el suelo
    float stride;   // metros (espacio del rig) que recorre la pata apoyada
    float offset[4];  // desfase de cada pata: delantera izq., delantera der., trasera izq., trasera der.
    float lift;     // altura del paso
    float gallop;   // 0..1
    float amount;   // 0 parado .. 1 en marcha
};

// Inversa de una matriz de giro y traslación (sin escala).
Mat4 rigidInverse(const Mat4& a) {
    Mat4 r = Mat4::identity();
    for (int i = 0; i < 3; ++i)
        for (int k = 0; k < 3; ++k) r.m[k * 4 + i] = a.m[i * 4 + k];
    Vec3 t = translationOf(a);
    r.m[12] = -(r.m[0] * t.x + r.m[4] * t.y + r.m[8] * t.z);
    r.m[13] = -(r.m[1] * t.x + r.m[5] * t.y + r.m[9] * t.z);
    r.m[14] = -(r.m[2] * t.x + r.m[6] * t.y + r.m[10] * t.z);
    return r;
}

HoundGait houndGait(const Enemy& e) {
    HoundGait g;
    float v = std::fabs(e.gaitSpeed) / std::max(e.size, 0.1f);  // en el espacio del rig
    float trot = smoothstep01((v - 0.9f) / 1.0f), gal = smoothstep01((v - 3.2f) / 1.6f);
    g.gallop = gal;
    g.amount = smoothstep01(v / 0.5f);
    g.freq = 0.9f + 0.38f * std::min(v, 6.5f);
    g.duty = lerpf(lerpf(0.62f, 0.48f, trot), 0.32f, gal);
    g.stride = std::min(v * g.duty / g.freq, 0.62f);
    const float walk[4] = {0.25f, 0.75f, 0.0f, 0.5f}, trotting[4] = {0.0f, 0.5f, 0.5f, 0.0f}, run[4] = {0.62f, 0.5f, 0.1f, 0.0f};
    for (int i = 0; i < 4; ++i) g.offset[i] = lerpf(lerpf(walk[i], trotting[i], trot), run[i], gal);
    g.lift = (0.06f + 0.05f * trot + 0.05f * gal) * g.amount;
    return g;
}

void animateHound(Enemy& e, float time, float dt, Vec3* r, Vec3& root) {
    float t = e.stateTime;
    // Velocidad (la del cuerpo, en su dirección) y fase de los pasos.
    bool moving = e.state == AiState::Idle || e.state == AiState::Chase || e.state == AiState::Stagger;
    float v = moving ? dot(flat(e.body.vel), e.forward()) : 0.0f;
    e.gaitSpeed += (v - e.gaitSpeed) * (1.0f - std::exp(-8.0f * dt));
    HoundGait g = houndGait(e);
    e.legPhase = std::fmod(e.legPhase + g.freq * dt, 1.0f);
    float ph = e.legPhase * 2.0f * kPi;
    float a = g.amount;

    // Postura base: algo agachado (las patas, dobladas), cabeza baja, cola tiesa. Respira.
    float breath = std::sin(time * 2.2f + e.seed);
    root.y = -0.06f;
    r[HdChest].x = 0.02f * breath;
    r[HdNeck].x = 0.3f;
    r[HdHead].x = 0.12f;
    r[HdJaw].x = 0.05f;
    r[HdTail].x = 0.3f;
    r[HdTail].y = 0.18f * std::sin(time * 1.5f + e.seed);

    // Marcha: al trote el lomo sube y baja dos veces por ciclo; al galope, una, con la espalda
    // que se encoge (patas recogidas bajo el cuerpo) y se estira (en el aire).
    float trotBob = 0.5f - 0.5f * std::cos(2.0f * ph);
    float flex = std::sin(ph + 0.6f);
    root.y += -0.02f * trotBob * a * (1.0f - g.gallop) + 0.045f * std::sin(ph - 0.4f) * g.gallop - 0.03f * g.gallop;
    r[HdPelvis].x += 0.1f * flex * g.gallop;
    r[HdChest].x += -0.2f * flex * g.gallop + 0.03f * std::sin(2.0f * ph) * a * (1.0f - g.gallop);
    r[HdPelvis].z = 0.04f * std::sin(ph) * a * (1.0f - g.gallop);  // al paso, la cadera se mece
    r[HdChest].y = -0.06f * std::sin(ph) * a * (1.0f - g.gallop);
    r[HdNeck].x += 0.12f * a - 0.15f * g.gallop;  // al galope, la cabeza al frente, estirada
    r[HdTail].x += -0.4f * g.gallop;              // y la cola detrás
    r[HdJaw].x += (0.12f + 0.06f * std::sin(time * 9.0f)) * g.gallop;  // jadea

    switch (e.state) {
        case AiState::Idle:
            if (a < 0.5f) {  // parado: husmea el suelo y de vez en cuando levanta la cabeza
                float sniff = std::sin(time * 1.1f + e.seed);
                float look = std::sin(time * 0.37f + e.seed * 2.0f);
                r[HdNeck].x += (0.25f + 0.2f * sniff) * (1.0f - a);
                r[HdHead].x += 0.06f * std::sin(time * 11.0f) * std::max(0.0f, sniff) * (1.0f - a);  // olfatea a golpecitos
                r[HdNeck].y = 0.35f * look * (1.0f - a);
                r[HdHead].y = 0.2f * look * (1.0f - a);
            } else {
                r[HdNeck].x += 0.3f;
            }
            break;
        case AiState::Chase: break;
        case AiState::Windup: {
            // Se agazapa: cuartos traseros abajo y atrás, cabeza al frente a ras del suelo,
            // fauces abiertas, gruñido que le hace temblar y la cola baja.
            float w = smoothstep01(t / 0.22f);
            root.y -= 0.12f * w;
            root.z -= 0.06f * w;
            r[HdPelvis].x -= 0.14f * w;
            r[HdChest].x += 0.12f * w;
            r[HdNeck].x = lerpf(r[HdNeck].x, -0.05f, w);
            r[HdHead].x = lerpf(r[HdHead].x, -0.1f, w);
            r[HdJaw].x = lerpf(r[HdJaw].x, 0.5f, w);
            r[HdChest].y += 0.04f * std::sin(time * 47.0f) * w;
            r[HdHead].z = 0.03f * std::sin(time * 61.0f) * w;
            r[HdTail].x = lerpf(r[HdTail].x, 0.8f, w);
            break;
        }
        case AiState::Attack: {
            // Salto a la garganta: se levanta de delante, el cuerpo se estira y la boca, muy
            // abierta, se cierra de golpe a mitad del salto.
            float u = clampf(t / e.def().attackTime, 0, 1);
            float rise = std::sin(u * kPi);
            r[HdPelvis].x -= 0.3f * rise - 0.1f * u;
            r[HdChest].x += 0.1f * u;
            r[HdNeck].x = -0.2f + 0.25f * u;
            r[HdHead].x = -0.1f;
            r[HdJaw].x = 0.7f * (1.0f - smoothstep01((u - 0.4f) / 0.12f));
            r[HdTail].x = -0.2f;
            root.y += 0.12f * rise;
            break;
        }
        case AiState::Recover: {
            // Cae y sacude la cabeza.
            float shake = std::sin(t * 22.0f) * std::exp(-t * 4.0f);
            r[HdNeck].x = 0.35f;
            r[HdHead].z = 0.45f * shake;
            r[HdNeck].y = 0.25f * shake;
            r[HdJaw].x = 0.15f;
            root.y -= 0.04f * std::exp(-t * 6.0f);
            break;
        }
        case AiState::Stagger: {
            float w = std::sin(clampf(t / 0.45f, 0, 1) * kPi);
            r[HdPelvis].x -= 0.2f * w;
            r[HdChest].x -= 0.25f * w;
            r[HdNeck].x -= 0.6f * w;
            r[HdHead].z = 0.3f * w;
            r[HdJaw].x = 0.3f * w;
            root.y -= 0.05f * w;
            break;
        }
        case AiState::Spawning:
        case AiState::Dying:
            root.y -= 0.28f;
            r[HdNeck].x = 0.8f;
            r[HdTail].x = 0.9f;
            break;
    }
    // El salto: las patas delanteras se estiran al frente y las traseras atrás.
    e.leap = moveTowards(e.leap, e.state == AiState::Attack ? 1.0f : 0.0f, dt / 0.08f);
    // La cabeza compensa el cabeceo del cuerpo (mira al frente).
    r[HdNeck].x -= 0.6f * (r[HdPelvis].x + r[HdChest].x) * (e.state == AiState::Attack ? 0.0f : 1.0f);
}

// Patas del sabueso: IK de dos huesos en el plano de cada pata (el codo y el corvejón, hacia
// atrás) y la pezuña plana sobre el suelo; en el aire, la delantera se dobla al levantarse.
void solveHoundLegs(Enemy& e, Pose& pose) {
    struct Leg {
        int up, low, paw;
        bool front;
        float phaseIndex;
    };
    const Leg legs[4] = {{HdFUpL, HdFLowL, HdFPawL, true, 0}, {HdFUpR, HdFLowR, HdFPawR, true, 1},
                         {HdHUpL, HdHLowL, HdHPawL, false, 2}, {HdHUpR, HdHLowR, HdHPawR, false, 3}};
    const float kPawHeight = 0.04f;  // la articulación de la pezuña sobre el suelo
    HoundGait g = houndGait(e);
    float dir = e.gaitSpeed >= 0 ? 1.0f : -1.0f;  // reculando, las pisadas van al revés
    for (const Leg& l : legs) {
        pose.rotation[l.up] = pose.rotation[l.low] = pose.rotation[l.paw] = {};
    }
    Mat4 m[kMaxJoints];
    computeWorldMatrices(kHoundRig, HdCount, pose, Mat4::identity(), m);
    for (const Leg& l : legs) {
        const JointDef& up = kHoundRig[l.up];
        Mat4 frame = m[up.parent] * translate(up.offset);  // la cadera/hombro antes de girar la pata
        Vec3 hip = translationOf(frame);
        float L1 = length(kHoundRig[l.low].offset), L2 = length(kHoundRig[l.paw].offset);
        // Dónde va la pezuña (espacio del rig: el suelo es y = 0).
        float p = std::fmod(e.legPhase + g.offset[int(l.phaseIndex)], 1.0f);
        float z, y = 0, swing = 0;
        if (p < g.duty) {
            z = g.stride * (0.5f - p / g.duty);
        } else {
            float s = (p - g.duty) / (1.0f - g.duty);
            z = g.stride * (-0.5f + s - std::sin(2.0f * kPi * s) / (2.0f * kPi));
            swing = std::sin(s * kPi);
            y = g.lift * (l.front ? 1.25f : 1.0f) * swing;
        }
        z *= dir;
        // Salto: delanteras al frente y arriba, traseras estiradas atrás.
        z += e.leap * (l.front ? 0.3f : -0.28f);
        y += e.leap * (l.front ? 0.22f : 0.06f);
        Vec3 target{hip.x, kPawHeight + y, hip.z + (l.front ? 0.05f : -0.03f) + z};
        Vec3 local = transformPoint(rigidInverse(frame), target);
        // En el plano YZ local: la pata cuelga hacia -Y y rotar +X la lleva hacia atrás.
        float d = clampf(std::sqrt(local.y * local.y + local.z * local.z), 0.08f, L1 + L2 - 0.002f);
        float phi = std::atan2(-local.z, -local.y);
        float alpha = std::acos(clampf((L1 * L1 + d * d - L2 * L2) / (2.0f * L1 * d), -1, 1));
        float beta = std::acos(clampf((L2 * L2 + d * d - L1 * L1) / (2.0f * L2 * d), -1, 1));
        float upper = phi + alpha, lower = phi - beta;  // la articulación del medio, hacia atrás
        // Inclinación del marco de la pata (el cuerpo cabeceando) para dejar la pezuña plana.
        Vec3 down = transformPoint(frame, {0, -1, 0}) - hip;
        float framePitch = std::atan2(-down.z, -down.y);
        float curl = (l.front ? 1.5f : 0.7f) * swing * g.amount + e.leap * (l.front ? 1.2f : 0.5f);
        pose.rotation[l.up].x = upper;
        pose.rotation[l.low].x = lower - upper;
        pose.rotation[l.paw].x = -(framePitch + lower) + curl;
    }
}

}  // namespace

const char* aiStateName(AiState s) {
    switch (s) {
        case AiState::Spawning: return "aparece";
        case AiState::Idle: return "vaga";
        case AiState::Chase: return "persigue";
        case AiState::Windup: return "prepara";
        case AiState::Attack: return "ataca";
        case AiState::Recover: return "recupera";
        case AiState::Stagger: return "tambalea";
        case AiState::Dying: return "se disipa";
    }
    return "?";
}

const EnemyDef& enemyDef(EnemyKind k) { return kDefs[int(k)]; }

Pose enemyBindPose(EnemyKind kind) {
    Pose p;
    Vec3* r = p.rotation;
    if (kind == EnemyKind::Dazzled) {
        r[DzArmL].z = 0.45f;
        r[DzArmR].z = -0.45f;
        r[DzThighL].z = 0.12f;
        r[DzThighR].z = -0.12f;
    } else if (kind == EnemyKind::Brute) {
        r[BrArmL].z = 0.4f;
        r[BrArmR].z = -0.4f;
        r[BrThighL].z = 0.12f;
        r[BrThighR].z = -0.12f;
    } else if (kind == EnemyKind::Hound) {  // patas algo abiertas para que no se fundan
        r[HdFUpL].z = 0.12f;
        r[HdFUpR].z = -0.12f;
        r[HdHUpL].z = 0.12f;
        r[HdHUpR].z = -0.12f;
        r[HdJaw].x = 0.15f;
    }
    return p;
}

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
    if (kind == EnemyKind::Moth) e.size = 1.4f;     // que se lean a la distancia de la cámara
    if (kind == EnemyKind::Firefly) e.size = 1.7f;
    animateEnemy(e, 0, 0);
    for (int i = 0; i < d.jointCount; ++i) e.prevJoints[i] = e.joints[i];
    e.prevPos = pos;
    return e;
}

Vec3 Enemy::center() const { return body.pos + Vec3{0, body.height * 0.5f, 0}; }

Mat4 Enemy::modelMatrix() const {
    // Al recibir un golpe se aplasta un instante (y se ensancha), y recupera la forma.
    float sq = squash;
    Mat4 m = translate(body.pos) * rotateY(yaw) * scale(Vec3{1.0f + 0.5f * sq, 1.0f - sq, 1.0f + 0.5f * sq} * size);
    if (state == AiState::Spawning) {  // crece desde el portal del suelo
        float s = smoothstep01(stateTime / 0.8f);
        return m * scale({s, s, s});
    }
    if (state == AiState::Dying) {  // gira y se encoge hasta desaparecer en luz
        float u = smoothstep01(stateTime / 0.5f);
        float s = 1.0f - 0.85f * u;
        float hh = def().height * 0.5f;  // se deshace hacia dentro, sin girar
        return m * translate({0, hh, 0}) * scale({s * (1.0f + 0.4f * u), s, s * (1.0f + 0.4f * u)}) * translate({0, -hh, 0});
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
        case EnemyKind::Dazzled:
            animateDazzled(e, time, r, root);
            r[DzHead].x -= 0.5f * (fwd + up);
            r[DzArmL].x -= 0.6f * fwd;
            r[DzArmR].x -= 0.6f * fwd;
            break;
        case EnemyKind::Brute:
            animateBrute(e, time, r, root);
            r[BrArmL].x -= 0.4f * fwd;
            r[BrArmR].x -= 0.4f * fwd;
            r[BrArmL].z += 0.3f * side;
            r[BrArmR].z += 0.3f * side;
            break;
        case EnemyKind::Watcher:
            animateWatcher(e, time, r, root);
            for (int k = 0; k < 4; ++k) {
                r[WaStrip1 + k].x -= 1.2f * (fwd + up);
                r[WaStrip1 + k].z += side;
            }
            break;
        case EnemyKind::Hound:
            animateHound(e, time, dt, r, root);
            r[HdTail].x -= 1.2f * up;
            r[HdTail].y += 1.5f * side;
            break;
        case EnemyKind::Moth:  // aleteo (9 Hz) y un cabeceo suave, como en el visor del paquete
        case EnemyKind::Firefly: {
            bool moth = e.kind == EnemyKind::Moth;
            float a = moth ? 0.55f : 0.4f, w = (moth ? 9.0f : 14.0f) * 2.0f * kPi;
            float flap = a * std::sin(time * w + e.seed * 7.0f);
            r[moth ? int(MoWingL) : int(FfWingL)].z = flap;
            r[moth ? int(MoWingR) : int(FfWingR)].z = -flap;
            r[moth ? int(MoBody) : int(FfCore)].x = (moth ? -0.3f : 0.2f) + 0.1f * std::sin(time * 1.3f + e.seed);
            root.y = 0.06f * std::sin(time * 1.7f + e.seed * 3.0f);
            break;
        }
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
    Pose blended = e.blendTime >= dur ? target : lerpPose(e.blendFrom, target, smoothstep01(e.blendTime / dur), d.jointCount);
    // Muelles: cada articulación persigue a su pose con algo de retraso y de rebote (las alas de
    // los enjambres, no: aletean demasiado deprisa). Las patas del sabueso las pone la IK.
    if (!e.springInit || dt <= 0 || isSwarm(e.kind)) {
        e.springInit = true;
        e.pose = blended;
        for (Vec3& v : e.springVel) v = {};
        e.rootSpringVel = {};
    } else {
        auto step = [dt](float x, float& v, float goal, float omega, float zeta, bool angle) {
            float diff = angle ? wrapAngle(goal - x) : goal - x;
            v = (v + omega * omega * dt * diff) / (1.0f + 2.0f * zeta * omega * dt + omega * omega * dt * dt);
            x += v * dt;
            return angle ? wrapAngle(x) : x;
        };
        bool firm = e.state == AiState::Dying || e.state == AiState::Spawning;
        for (int j = 0; j < d.jointCount; ++j) {
            float omega = firm ? 40.0f : j <= 1 ? 26.0f : 20.0f, zeta = firm ? 1.0f : j <= 1 ? 0.6f : 0.55f;
            Vec3& x = e.pose.rotation[j];
            Vec3& v = e.springVel[j];
            const Vec3& g = blended.rotation[j];
            x.x = step(x.x, v.x, g.x, omega, zeta, true);
            x.y = step(x.y, v.y, g.y, omega, zeta, true);
            x.z = step(x.z, v.z, g.z, omega, zeta, true);
        }
        Vec3& ro = e.pose.rootOffset;
        ro.x = step(ro.x, e.rootSpringVel.x, blended.rootOffset.x, 30.0f, 0.8f, false);
        ro.y = step(ro.y, e.rootSpringVel.y, blended.rootOffset.y, 30.0f, 0.8f, false);
        ro.z = step(ro.z, e.rootSpringVel.z, blended.rootOffset.z, 30.0f, 0.8f, false);
    }
    if (e.kind == EnemyKind::Hound) solveHoundLegs(e, e.pose);
    e.squash *= std::exp(-12.0f * dt);
    computeWorldMatrices(d.rig, d.jointCount, e.pose, e.modelMatrix(), e.joints);
}

void hitReaction(Enemy& e, Vec3 push, float strength) {
    Vec3 p = flat(push);
    if (length(p) < 0.01f || isSwarm(e.kind)) return;
    p = normalize(p);
    Vec3 right{std::cos(e.yaw), 0, -std::sin(e.yaw)};
    float along = dot(p, e.forward()), side = dot(p, right);
    float k = (e.boss ? 0.5f : 1.0f) * strength;
    // Rotar +X inclina la parte de arriba hacia delante; +Z, hacia -X (su derecha).
    e.springVel[0].x += 5.0f * along * k;
    e.springVel[0].z -= 5.0f * side * k;
    if (e.def().jointCount > 1) {
        e.springVel[1].x += 9.0f * along * k;
        e.springVel[1].z -= 8.0f * side * k;
    }
    e.squash = std::min(0.35f, e.squash + 0.22f * k);
}
