#include "physics_jolt.h"

// Jolt usa su propio Vec3/Mat44 (namespace JPH): aquí no se hace `using namespace JPH`
// para no chocar con los tipos del juego.
#include <Jolt/Jolt.h>

#include <Jolt/Core/Factory.h>
#include <Jolt/Core/JobSystemSingleThreaded.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Constraints/HingeConstraint.h>
#include <Jolt/Physics/Constraints/SwingTwistConstraint.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Ragdoll/Ragdoll.h>
#include <Jolt/RegisterTypes.h>
#include <Jolt/Skeleton/Skeleton.h>

#include <mutex>
#include <vector>

namespace {

// Dos capas: lo estático (nivel) y lo que se mueve (ragdolls, cajas, cápsulas
// cinemáticas). Lo estático no se comprueba contra sí mismo.
constexpr JPH::ObjectLayer kStaticLayer = 0, kMovingLayer = 1;
const JPH::BroadPhaseLayer kStaticBP(0), kMovingBP(1);

class ObjectLayerPairs final : public JPH::ObjectLayerPairFilter {
public:
    bool ShouldCollide(JPH::ObjectLayer a, JPH::ObjectLayer b) const override {
        return a == kMovingLayer || b == kMovingLayer;
    }
};

class BroadPhaseLayers final : public JPH::BroadPhaseLayerInterface {
public:
    JPH::uint GetNumBroadPhaseLayers() const override { return 2; }
    JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer l) const override {
        return l == kStaticLayer ? kStaticBP : kMovingBP;
    }
#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
    const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer l) const override {
        return l == kStaticBP ? "estatico" : "movil";
    }
#endif
};

class ObjectVsBroadPhase final : public JPH::ObjectVsBroadPhaseLayerFilter {
public:
    bool ShouldCollide(JPH::ObjectLayer l, JPH::BroadPhaseLayer bp) const override {
        return l == kMovingLayer || bp == kMovingBP;
    }
};

// Solo cuerpos dinámicos (para rayos de balas y golpes).
class DynamicOnly final : public JPH::BodyFilter {
public:
    bool ShouldCollideLocked(const JPH::Body& b) const override { return b.IsDynamic(); }
};

JPH::Vec3 toJ(Vec3 v) { return {v.x, v.y, v.z}; }
Vec3 fromJ(JPH::Vec3Arg v) { return {v.GetX(), v.GetY(), v.GetZ()}; }
JPH::Mat44 toJ(const Mat4& m) {
    return {JPH::Vec4(m.m[0], m.m[1], m.m[2], m.m[3]), JPH::Vec4(m.m[4], m.m[5], m.m[6], m.m[7]),
            JPH::Vec4(m.m[8], m.m[9], m.m[10], m.m[11]), JPH::Vec4(m.m[12], m.m[13], m.m[14], m.m[15])};
}
Mat4 fromJ(const JPH::Mat44& j) {
    Mat4 r;
    for (int c = 0; c < 4; ++c) {
        JPH::Vec4 col = j.GetColumn4(c);
        for (int k = 0; k < 4; ++k) r.m[c * 4 + k] = col[k];
    }
    return r;
}
Vec3 column(const Mat4& m, int c) { return {m.m[c * 4], m.m[c * 4 + 1], m.m[c * 4 + 2]}; }
// `v` sin su componente sobre el eje unitario `axis`, normalizado.
Vec3 perpendicular(Vec3 v, Vec3 axis) { return normalize(v - axis * dot(v, axis)); }

void initJoltOnce() {
    static std::once_flag once;
    std::call_once(once, [] {
        JPH::RegisterDefaultAllocator();
        JPH::Factory::sInstance = new JPH::Factory();
        JPH::RegisterTypes();
    });
}

// Ragdoll de 11 cuerpos. Cada parte va pegada a una articulación del esqueleto del juego
// ("ancla": el cuerpo empieza con su matriz) y la une a su padre una restricción en la
// posición de esa articulación: swing-twist (cono + giro) o bisagra (codos y rodillas).
enum class PartShape { Box, Capsule, Sphere };
enum class Link { None, SwingTwist, Hinge };
struct PartDef {
    const char* name;
    Joint anchor;
    int parent;
    PartShape shape;
    Vec3 center;  // en el espacio del ancla
    Vec3 size;    // caja: semiejes; cápsula: (radio, semialtura del cilindro); esfera: (radio)
    Link link;
    bool up;      // eje de giro de la restricción: +Y (tronco) o -Y (miembros que cuelgan)
    float a, b;   // swing-twist: medio cono normal y plano; bisagra: límites
    float twist;  // swing-twist: giro máximo a cada lado
};
// clang-format off
const PartDef kParts[] = {
    {"pelvis",     Pelvis,    -1, PartShape::Box,     {0, 0, 0},         {0.15f, 0.09f, 0.10f},  Link::None,       true,  0, 0, 0},
    {"torso",      Spine,      0, PartShape::Box,     {0, 0.20f, 0},     {0.16f, 0.19f, 0.10f},  Link::SwingTwist, true,  0.35f, 0.35f, 0.35f},
    {"cabeza",     Neck,       1, PartShape::Sphere,  {0, 0.17f, 0.01f}, {0.10f, 0, 0},          Link::SwingTwist, true,  0.6f, 0.6f, 0.7f},
    {"brazo_i",    ShoulderL,  1, PartShape::Capsule, {0, -0.14f, 0},    {0.05f, 0.09f, 0},      Link::SwingTwist, false, 2.0f, 2.0f, 0.8f},
    {"antebrazo_i",ElbowL,     3, PartShape::Capsule, {0, -0.18f, 0},    {0.045f, 0.135f, 0},    Link::Hinge,      false, -2.6f, 0.0f, 0},
    {"brazo_d",    ShoulderR,  1, PartShape::Capsule, {0, -0.14f, 0},    {0.05f, 0.09f, 0},      Link::SwingTwist, false, 2.0f, 2.0f, 0.8f},
    {"antebrazo_d",ElbowR,     5, PartShape::Capsule, {0, -0.18f, 0},    {0.045f, 0.135f, 0},    Link::Hinge,      false, -2.6f, 0.0f, 0},
    {"muslo_i",    HipL,       0, PartShape::Capsule, {0, -0.215f, 0},   {0.075f, 0.14f, 0},     Link::SwingTwist, false, 1.2f, 0.8f, 0.4f},
    {"pierna_i",   KneeL,      7, PartShape::Capsule, {0, -0.24f, 0},    {0.06f, 0.18f, 0},      Link::Hinge,      false, 0.0f, 2.5f, 0},
    {"muslo_d",    HipR,       0, PartShape::Capsule, {0, -0.215f, 0},   {0.075f, 0.14f, 0},     Link::SwingTwist, false, 1.2f, 0.8f, 0.4f},
    {"pierna_d",   KneeR,      9, PartShape::Capsule, {0, -0.24f, 0},    {0.06f, 0.18f, 0},      Link::Hinge,      false, 0.0f, 2.5f, 0},
};
// Parte que mueve cada articulación del juego (las manos y pies van con su antebrazo/pierna).
const int kJointPart[JointCount] = {
    0, 1, 1, 2, 2,      // pelvis, spine, chest, neck, head
    3, 4, 4,            // hombro, codo, muñeca izquierdos
    5, 6, 6,            // derechos
    7, 8, 8, 8,         // cadera, rodilla, tobillo, punta izquierdas
    9, 10, 10, 10,      // derechas
};
// clang-format on
constexpr int kPartCount = int(sizeof(kParts) / sizeof(kParts[0]));

JPH::Ref<JPH::Shape> partShape(const PartDef& p) {
    JPH::Ref<JPH::Shape> s;
    switch (p.shape) {
        case PartShape::Box: s = new JPH::BoxShape(toJ(p.size), 0.02f); break;
        case PartShape::Capsule: s = new JPH::CapsuleShape(p.size.y, p.size.x); break;
        case PartShape::Sphere: s = new JPH::SphereShape(p.size.x); break;
    }
    return new JPH::RotatedTranslatedShape(toJ(p.center), JPH::Quat::sIdentity(), s);
}

}  // namespace

struct RigidWorld::Impl {
    struct RagdollRec {
        JPH::Ref<JPH::RagdollSettings> settings;
        JPH::Ref<JPH::Ragdoll> ragdoll;
        JPH::Mat44 offsets[JointCount];  // articulación respecto a su cuerpo
    };
    struct Crate {
        JPH::BodyID id;
        Vec3 half;
    };
    struct Proxy {
        JPH::BodyID id;
        bool enabled = true, teleport = true;
    };

    // Los filtros tienen que vivir más que el PhysicsSystem (los guarda por referencia).
    BroadPhaseLayers broadPhaseLayers;
    ObjectVsBroadPhase objectVsBroadPhase;
    ObjectLayerPairs objectPairs;
    JPH::TempAllocatorImpl temp{4 * 1024 * 1024};
    JPH::JobSystemSingleThreaded jobs{JPH::cMaxPhysicsJobs};  // un hilo: determinista
    JPH::PhysicsSystem system;
    std::vector<JPH::BodyID> statics;
    std::vector<RagdollRec> ragdolls;  // los huecos (ragdoll nulo) se reutilizan
    std::vector<Crate> crates;
    std::vector<Proxy> proxies;
    JPH::CollisionGroup::GroupID nextGroup = 1;

    Impl() {
        system.Init(2048, 0, 2048, 2048, broadPhaseLayers, objectVsBroadPhase, objectPairs);
        system.SetGravity(JPH::Vec3(0, -kGravity, 0));
    }
    ~Impl() {
        for (RagdollRec& r : ragdolls)
            if (r.ragdoll) r.ragdoll->RemoveFromPhysicsSystem();
        ragdolls.clear();
        JPH::BodyInterface& bi = system.GetBodyInterface();
        auto destroy = [&](JPH::BodyID id) {
            bi.RemoveBody(id);
            bi.DestroyBody(id);
        };
        for (const Crate& c : crates) destroy(c.id);
        for (const Proxy& p : proxies) destroy(p.id);
        for (JPH::BodyID id : statics) destroy(id);
    }
    JPH::BodyInterface& bodies() { return system.GetBodyInterface(); }
    const JPH::BodyInterface& bodies() const { return system.GetBodyInterface(); }

    template <typename F>
    void forEachDynamic(F f) const {
        for (const Crate& c : crates) f(c.id);
        for (const RagdollRec& r : ragdolls)
            if (r.ragdoll)
                for (JPH::BodyID id : r.ragdoll->GetBodyIDs()) f(id);
    }
};

RigidWorld::RigidWorld() : impl_(std::make_unique<Impl>()) { initJoltOnce(); }
RigidWorld::~RigidWorld() = default;
RigidWorld::RigidWorld(RigidWorld&&) noexcept = default;
RigidWorld& RigidWorld::operator=(RigidWorld&&) noexcept = default;

void RigidWorld::build(const World& world) {
    JPH::BodyInterface& bi = impl_->bodies();
    auto addStatic = [&](Vec3 center, Vec3 half) {
        JPH::BodyCreationSettings s(new JPH::BoxShape(toJ(half), 0.0f), toJ(center), JPH::Quat::sIdentity(),
                                    JPH::EMotionType::Static, kStaticLayer);
        s.mFriction = 0.8f;
        impl_->statics.push_back(bi.CreateAndAddBody(s, JPH::EActivation::DontActivate));
    };
    addStatic({0, -0.5f, 0}, {200, 0.5f, 200});  // suelo
    for (const AABB& b : world.solids) addStatic(b.center(), b.halfExtents());
    impl_->system.OptimizeBroadPhase();
}

void RigidWorld::step(float dt) { impl_->system.Update(dt, 1, &impl_->temp, &impl_->jobs); }

int RigidWorld::createRagdoll(const Mat4 joints[JointCount], Vec3 velocity) {
    JPH::Ref<JPH::RagdollSettings> settings = new JPH::RagdollSettings;
    settings->mSkeleton = new JPH::Skeleton;
    settings->mParts.resize(kPartCount);
    for (int i = 0; i < kPartCount; ++i) {
        const PartDef& def = kParts[i];
        settings->mSkeleton->AddJoint(def.name, def.parent);
        const Mat4& anchor = joints[def.anchor];
        JPH::RagdollSettings::Part& part = settings->mParts[size_t(i)];
        part.SetShape(partShape(def));
        part.mPosition = toJ(translationOf(anchor));
        part.mRotation = toJ(anchor).GetQuaternion().Normalized();
        part.mMotionType = JPH::EMotionType::Dynamic;
        part.mObjectLayer = kMovingLayer;
        part.mFriction = 0.8f;
        part.mLinearDamping = 0.1f;
        part.mAngularDamping = 0.6f;
        if (def.parent < 0) continue;

        // La restricción se define respecto a la postura de reposo (miembros colgando y
        // tronco vertical en el espacio del padre), no respecto a la pose actual: así los
        // límites son anatómicos aunque muera con el brazo levantado.
        const Mat4& parentAnchor = joints[kParts[def.parent].anchor];
        const Mat4& restFrame = def.anchor == ShoulderL || def.anchor == ShoulderR ? joints[Chest] : parentAnchor;
        Vec3 pos = translationOf(anchor);
        float sign = def.up ? 1.0f : -1.0f;
        Vec3 axis1 = normalize(column(restFrame, 1) * sign), axis2 = normalize(column(anchor, 1) * sign);
        if (def.link == Link::SwingTwist) {
            auto* c = new JPH::SwingTwistConstraintSettings;
            c->mSpace = JPH::EConstraintSpace::WorldSpace;
            c->mPosition1 = c->mPosition2 = toJ(pos);
            c->mTwistAxis1 = toJ(axis1);
            c->mTwistAxis2 = toJ(axis2);
            c->mPlaneAxis1 = toJ(perpendicular(column(restFrame, 2), axis1));
            c->mPlaneAxis2 = toJ(perpendicular(column(anchor, 2), axis2));
            c->mNormalHalfConeAngle = def.a;
            c->mPlaneHalfConeAngle = def.b;
            c->mTwistMinAngle = -def.twist;
            c->mTwistMaxAngle = def.twist;
            part.mToParent = c;
        } else {
            // Bisagra sobre el eje X de la articulación: el ángulo es el que forman los
            // huesos (rodilla > 0 al doblarse, codo < 0, como en la animación).
            auto* c = new JPH::HingeConstraintSettings;
            c->mSpace = JPH::EConstraintSpace::WorldSpace;
            c->mPoint1 = c->mPoint2 = toJ(pos);
            Vec3 hinge1 = normalize(column(parentAnchor, 0)), hinge2 = normalize(column(anchor, 0));
            c->mHingeAxis1 = toJ(hinge1);
            c->mHingeAxis2 = toJ(hinge2);
            c->mNormalAxis1 = toJ(perpendicular(column(parentAnchor, 1) * -1.0f, hinge1));
            c->mNormalAxis2 = toJ(perpendicular(column(anchor, 1) * -1.0f, hinge2));
            c->mLimitsMin = def.a;
            c->mLimitsMax = def.b;
            part.mToParent = c;
        }
    }
    settings->Stabilize();
    settings->DisableParentChildCollisions();

    JPH::Ref<JPH::Ragdoll> ragdoll = settings->CreateRagdoll(impl_->nextGroup++, 0, &impl_->system);
    if (!ragdoll) return -1;
    ragdoll->AddToPhysicsSystem(JPH::EActivation::Activate);
    ragdoll->SetLinearVelocity(toJ(velocity));

    Impl::RagdollRec rec;
    rec.settings = settings;
    rec.ragdoll = ragdoll;
    for (int j = 0; j < JointCount; ++j) {
        const Mat4& anchor = joints[kParts[kJointPart[j]].anchor];
        rec.offsets[j] = toJ(anchor).InversedRotationTranslation() * toJ(joints[j]);
    }
    for (size_t i = 0; i < impl_->ragdolls.size(); ++i)
        if (!impl_->ragdolls[i].ragdoll) {
            impl_->ragdolls[i] = rec;
            return int(i);
        }
    impl_->ragdolls.push_back(rec);
    return int(impl_->ragdolls.size() - 1);
}

void RigidWorld::destroyRagdoll(int id) {
    if (id < 0 || size_t(id) >= impl_->ragdolls.size() || !impl_->ragdolls[size_t(id)].ragdoll) return;
    Impl::RagdollRec& r = impl_->ragdolls[size_t(id)];
    r.ragdoll->RemoveFromPhysicsSystem();
    r = Impl::RagdollRec{};  // el destructor de Ragdoll destruye sus cuerpos
}

void RigidWorld::ragdollJoints(int id, Mat4 out[JointCount]) const {
    const Impl::RagdollRec& r = impl_->ragdolls[size_t(id)];
    const JPH::BodyInterface& bi = impl_->bodies();
    JPH::Mat44 bodyWorld[kPartCount];
    for (int i = 0; i < kPartCount; ++i) bodyWorld[i] = bi.GetWorldTransform(r.ragdoll->GetBodyID(i));
    for (int j = 0; j < JointCount; ++j) out[j] = fromJ(bodyWorld[kJointPart[j]] * r.offsets[j]);
}

Vec3 RigidWorld::ragdollPelvis(int id) const {
    const Impl::RagdollRec& r = impl_->ragdolls[size_t(id)];
    return fromJ(impl_->bodies().GetPosition(r.ragdoll->GetBodyID(0)));
}

void RigidWorld::addCrate(Vec3 center, Vec3 half, float mass) {
    JPH::BodyCreationSettings s(new JPH::BoxShape(toJ(half), 0.02f), toJ(center), JPH::Quat::sIdentity(),
                                JPH::EMotionType::Dynamic, kMovingLayer);
    s.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
    s.mMassPropertiesOverride.mMass = mass;
    s.mFriction = 0.7f;
    s.mAngularDamping = 0.3f;
    impl_->crates.push_back({impl_->bodies().CreateAndAddBody(s, JPH::EActivation::Activate), half});
}

size_t RigidWorld::crateCount() const { return impl_->crates.size(); }
Mat4 RigidWorld::crateTransform(size_t i) const { return fromJ(impl_->bodies().GetWorldTransform(impl_->crates[i].id)); }
Vec3 RigidWorld::crateHalf(size_t i) const { return impl_->crates[i].half; }

int RigidWorld::addProxy(float radius, float height) {
    float half = height * 0.5f;
    JPH::Ref<JPH::Shape> capsule = new JPH::CapsuleShape(half - radius, radius);
    JPH::BodyCreationSettings s(new JPH::RotatedTranslatedShape(JPH::Vec3(0, half, 0), JPH::Quat::sIdentity(), capsule),
                                JPH::Vec3(0, -50, 0), JPH::Quat::sIdentity(), JPH::EMotionType::Kinematic,
                                kMovingLayer);
    impl_->proxies.push_back({impl_->bodies().CreateAndAddBody(s, JPH::EActivation::Activate)});
    return int(impl_->proxies.size() - 1);
}

void RigidWorld::moveProxy(int id, Vec3 feet, float dt) {
    Impl::Proxy& p = impl_->proxies[size_t(id)];
    if (!p.enabled) return;
    if (p.teleport) {  // al aparecer: sin velocidad, para no lanzar lo que haya cerca
        impl_->bodies().SetPositionAndRotation(p.id, toJ(feet), JPH::Quat::sIdentity(), JPH::EActivation::Activate);
        p.teleport = false;
        return;
    }
    impl_->bodies().MoveKinematic(p.id, toJ(feet), JPH::Quat::sIdentity(), dt);
}

void RigidWorld::setProxyEnabled(int id, bool enabled) {
    Impl::Proxy& p = impl_->proxies[size_t(id)];
    if (p.enabled == enabled) return;
    p.enabled = enabled;
    p.teleport = true;
    if (!enabled)  // fuera del mundo mientras está desactivado (muerto)
        impl_->bodies().SetPositionAndRotation(p.id, JPH::Vec3(0, -50, 0), JPH::Quat::sIdentity(),
                                               JPH::EActivation::DontActivate);
}

bool RigidWorld::raycast(Vec3 from, Vec3 to, float& t, Vec3& normal) const {
    JPH::RRayCast ray(toJ(from), toJ(to - from));
    JPH::RayCastResult hit;
    DynamicOnly filter;
    if (!impl_->system.GetNarrowPhaseQuery().CastRay(ray, hit, {}, {}, filter)) return false;
    t = hit.mFraction;
    JPH::BodyLockRead lock(impl_->system.GetBodyLockInterface(), hit.mBodyID);
    normal = lock.Succeeded() ? fromJ(lock.GetBody().GetWorldSpaceSurfaceNormal(hit.mSubShapeID2, ray.GetPointOnRay(t)))
                              : normalize(from - to);
    return true;
}

bool RigidWorld::impulseAlong(Vec3 from, Vec3 to, float impulse) {
    JPH::RRayCast ray(toJ(from), toJ(to - from));
    JPH::RayCastResult hit;
    DynamicOnly filter;
    if (!impl_->system.GetNarrowPhaseQuery().CastRay(ray, hit, {}, {}, filter)) return false;
    impl_->bodies().AddImpulse(hit.mBodyID, toJ(normalize(to - from) * impulse), ray.GetPointOnRay(hit.mFraction));
    return true;
}

void RigidWorld::explosion(Vec3 center, float radius, float speed, float lift) {
    JPH::BodyInterface& bi = impl_->bodies();
    impl_->forEachDynamic([&](JPH::BodyID id) {
        Vec3 d = fromJ(bi.GetCenterOfMassPosition(id)) - center;
        float dist = length(d);
        if (dist >= radius) return;
        float k = 1.0f - dist / radius;
        Vec3 dir = dist > 1e-3f ? d * (1.0f / dist) : Vec3{0, 1, 0};
        bi.AddLinearVelocity(id, toJ(dir * (speed * k) + Vec3{0, lift * k, 0}));
        bi.ActivateBody(id);
    });
}

void RigidWorld::push(Vec3 center, float radius, Vec3 velocity) {
    JPH::BodyInterface& bi = impl_->bodies();
    impl_->forEachDynamic([&](JPH::BodyID id) {
        float dist = length(fromJ(bi.GetCenterOfMassPosition(id)) - center);
        if (dist >= radius) return;
        bi.AddLinearVelocity(id, toJ(velocity * (1.0f - 0.5f * dist / radius)));
        bi.ActivateBody(id);
    });
}

size_t RigidWorld::dynamicBodyCount() const {
    size_t n = 0;
    impl_->forEachDynamic([&](JPH::BodyID) { ++n; });
    return n;
}
