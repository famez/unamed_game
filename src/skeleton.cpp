#include "skeleton.h"

// Proporciones aproximadas de un adulto de ~1,77 m. El personaje mira hacia +Z,
// así que su lado izquierdo es +X.
// clang-format off
const JointDef kSkeleton[JointCount] = {
//   nombre        padre      offset                    centro caja             semiejes caja
    {"pelvis",     -1,        {0.00f, 0.95f, 0.00f},   {0, 0.00f, 0},          {0.14f, 0.07f, 0.09f}},
    {"spine",      Pelvis,    {0.00f, 0.12f, 0.00f},   {0, 0.09f, 0},          {0.13f, 0.09f, 0.08f}},
    {"chest",      Spine,     {0.00f, 0.18f, 0.00f},   {0, 0.11f, 0},          {0.17f, 0.12f, 0.10f}},
    {"neck",       Chest,     {0.00f, 0.22f, 0.00f},   {0, 0.05f, 0},          {0.04f, 0.05f, 0.04f}},
    {"head",       Neck,      {0.00f, 0.10f, 0.00f},   {0, 0.11f, 0.01f},      {0.09f, 0.11f, 0.10f}},

    {"shoulder_l", Chest,     {0.19f, 0.18f, 0.00f},   {0, -0.14f, 0},         {0.05f, 0.14f, 0.05f}},
    {"elbow_l",    ShoulderL, {0.00f, -0.28f, 0.00f},  {0, -0.13f, 0},         {0.04f, 0.13f, 0.04f}},
    {"wrist_l",    ElbowL,    {0.00f, -0.26f, 0.00f},  {0, -0.07f, 0},         {0.03f, 0.07f, 0.045f}},

    {"shoulder_r", Chest,     {-0.19f, 0.18f, 0.00f},  {0, -0.14f, 0},         {0.05f, 0.14f, 0.05f}},
    {"elbow_r",    ShoulderR, {0.00f, -0.28f, 0.00f},  {0, -0.13f, 0},         {0.04f, 0.13f, 0.04f}},
    {"wrist_r",    ElbowR,    {0.00f, -0.26f, 0.00f},  {0, -0.07f, 0},         {0.03f, 0.07f, 0.045f}},

    {"hip_l",      Pelvis,    {0.10f, -0.05f, 0.00f},  {0, -0.215f, 0},        {0.07f, 0.215f, 0.07f}},
    {"knee_l",     HipL,      {0.00f, -0.43f, 0.00f},  {0, -0.21f, 0},         {0.055f, 0.21f, 0.055f}},
    {"ankle_l",    KneeL,     {0.00f, -0.42f, 0.00f},  {0, -0.02f, 0.05f},     {0.045f, 0.025f, 0.10f}},
    {"toe_l",      AnkleL,    {0.00f, -0.05f, 0.14f},  {},                     {}},

    {"hip_r",      Pelvis,    {-0.10f, -0.05f, 0.00f}, {0, -0.215f, 0},        {0.07f, 0.215f, 0.07f}},
    {"knee_r",     HipR,      {0.00f, -0.43f, 0.00f},  {0, -0.21f, 0},         {0.055f, 0.21f, 0.055f}},
    {"ankle_r",    KneeR,     {0.00f, -0.42f, 0.00f},  {0, -0.02f, 0.05f},     {0.045f, 0.025f, 0.10f}},
    {"toe_r",      AnkleR,    {0.00f, -0.05f, 0.14f},  {},                     {}},
};
// clang-format on

void computeWorldMatrices(const Pose& pose, const Mat4& model, Mat4 out[JointCount]) {
    for (int i = 0; i < JointCount; ++i) {
        const JointDef& j = kSkeleton[i];
        const Vec3& r = pose.rotation[i];
        Vec3 offset = j.parent < 0 ? j.offset + pose.rootOffset : j.offset;
        Mat4 local = translate(offset) * rotateY(r.y) * rotateX(r.x) * rotateZ(r.z);
        // Como los padres van antes que los hijos, out[parent] ya está calculado.
        out[i] = (j.parent < 0 ? model : out[j.parent]) * local;
    }
}
