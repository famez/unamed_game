#include "enemy.h"

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

#define COUNT(a) int(sizeof(a) / sizeof((a)[0]))
const EnemyDef kDefs[int(EnemyKind::Count)] = {
//   nombre        rig          n        formas          n                     vida  vel.  radio alto  aggro alc.  prep. ataque recup. espera daño golpe vuela altura munny sombra
    {"Deslumbrado", kDazzledRig, DzCount, kDazzledShapes, COUNT(kDazzledShapes), 45, 5.0f, 0.35f, 1.9f, 20, 2.5f, 0.32f, 0.55f, 0.45f, 0.7f,  11, 5.0f, false, 0,    3, 6,  false},
    {"Coloso",      kBruteRig,   BrCount, kBruteShapes,   COUNT(kBruteShapes),   110, 2.7f, 0.6f, 2.2f, 16, 3.4f, 0.55f, 1.00f, 0.70f, 1.3f,  22, 9.0f, false, 0,    6, 10, true},
    {"Vigía",       kWatcherRig, WaCount, kWatcherShapes, COUNT(kWatcherShapes), 32, 3.6f, 0.40f, 0.7f, 22, 10.0f, 0.60f, 0.30f, 0.60f, 1.3f, 13, 4.0f, true,  2.6f, 4, 8,  false},
    {"Sabueso",     kHoundRig,   HdCount, kHoundShapes,   COUNT(kHoundShapes),   38, 6.2f, 0.42f, 0.9f, 22, 3.0f, 0.28f, 0.40f, 0.50f, 0.8f,  10, 4.0f, false, 0,    2, 5,  true},
};
#undef COUNT
// clang-format on

static_assert(DzCount <= kMaxJoints && BrCount <= kMaxJoints && WaCount <= kMaxJoints && HdCount <= kMaxJoints,
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

void animateHound(Enemy& e, float time, Vec3* r, Vec3& root) {
    float t = e.stateTime;
    // Cabeza baja, olfateando; la cola rígida.
    r[HdNeck].x = 0.35f + 0.08f * std::sin(time * 4.0f + e.seed);
    r[HdHead].x = 0.15f;
    r[HdJaw].x = 0.05f;
    r[HdTail].x = 0.25f;
    r[HdTail].y = 0.15f * std::sin(time * 1.5f + e.seed);
    switch (e.state) {
        case AiState::Chase:
        case AiState::Idle: {
            // Trote: pares diagonales (delantera izquierda con trasera derecha); al correr
            // la espalda se arquea y se estira.
            float run = e.state == AiState::Chase ? 1.0f : 0.35f;
            float a = std::sin(e.phase), b = std::sin(e.phase + kPi);
            r[HdFUpL].x = -0.7f * a * run;
            r[HdHUpR].x = -0.7f * a * run;
            r[HdFUpR].x = -0.7f * b * run;
            r[HdHUpL].x = -0.7f * b * run;
            r[HdFLowL].x = 0.6f * std::max(0.0f, -a) * run;
            r[HdFLowR].x = 0.6f * std::max(0.0f, -b) * run;
            r[HdHLowL].x = -0.5f * std::max(0.0f, b) * run;
            r[HdHLowR].x = -0.5f * std::max(0.0f, a) * run;
            r[HdPelvis].x = 0.1f * std::sin(e.phase * 2.0f) * run;
            r[HdChest].x = -0.1f * std::sin(e.phase * 2.0f) * run;
            r[HdNeck].x += 0.2f * run;
            root.y = 0.05f * std::fabs(std::cos(e.phase)) * run;
            if (e.state == AiState::Idle) r[HdNeck].x += 0.4f;  // husmea el suelo
            break;
        }
        case AiState::Windup: {
            // Se agazapa: cuartos traseros abajo, cabeza al frente, fauces abiertas.
            float w = smoothstep01(t / 0.2f);
            root.y = -0.16f * w;
            r[HdHUpL].x = r[HdHUpR].x = -0.6f * w;
            r[HdHLowL].x = r[HdHLowR].x = 0.9f * w;
            r[HdFUpL].x = r[HdFUpR].x = 0.3f * w;
            r[HdFLowL].x = r[HdFLowR].x = -0.5f * w;
            r[HdNeck].x = 0.05f;
            r[HdHead].x = -0.15f;
            r[HdJaw].x = 0.45f * w;
            r[HdChest].y = 0.05f * std::sin(time * 50.0f) * w;
            break;
        }
        case AiState::Attack: {
            // Salto: estirado, patas delanteras al frente, mordisco que se cierra.
            float u = clampf(t / 0.3f, 0, 1);
            r[HdFUpL].x = r[HdFUpR].x = -1.3f;
            r[HdHUpL].x = r[HdHUpR].x = 0.9f;
            r[HdNeck].x = -0.1f;
            r[HdJaw].x = lerpf(0.6f, 0.0f, smoothstep01((u - 0.5f) * 2.0f));
            break;
        }
        case AiState::Recover:
            r[HdNeck].x = 0.5f;
            r[HdJaw].x = 0.1f;
            break;
        case AiState::Stagger: {
            float w = std::sin(clampf(t / 0.45f, 0, 1) * kPi);
            r[HdChest].x = -0.4f * w;
            r[HdNeck].x = -0.4f * w;
            r[HdFUpL].x = r[HdFUpR].x = -0.6f * w;
            break;
        }
        case AiState::Spawning:
        case AiState::Dying:
            r[HdNeck].x = 0.8f;
            root.y = -0.2f;
            break;
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
    animateEnemy(e, 0, 0);
    for (int i = 0; i < d.jointCount; ++i) e.prevJoints[i] = e.joints[i];
    e.prevPos = pos;
    return e;
}

Vec3 Enemy::center() const { return body.pos + Vec3{0, body.height * 0.5f, 0}; }

Mat4 Enemy::modelMatrix() const {
    Mat4 m = translate(body.pos) * rotateY(yaw) * scale({size, size, size});
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
            animateHound(e, time, r, root);
            r[HdTail].x -= 1.2f * up;
            r[HdTail].y += 1.5f * side;
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
