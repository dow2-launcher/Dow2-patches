// aim_glue.h - WeaponUtil::CalculateScatterPosition (below) and ShootingWeapon's aim math run as DOW2.exe's own code (lift/lift_aim.c, made by lift/lift13.py from
// 0xa49d10 GetWorldPositionHeading, 0xa49450 GetAnglesToTarget, 0xa49910 ConvertWorldSpacePointToWeaponSpace and what they
// call: 0xa49b60 hardpoint -> world, 0xa49330 the pitch angle, 0x6757c0/e0 cosf/sinf; msvcr80 sin/cos/asin/acos = msmath.c).
// Windows inlines Math::MakeRotateY/MakeRotateAboutAxis into GetWorldPositionHeading with the sin/cos kept in registers
// and adds every vector * matrix in its own order (e.g. ((z*Zx + y*Yx) + x*Xx)), |v| as (x^2 + z^2) + y^2 ...: the port
// does it in source order and differs in the last bits.
// Windows' weapon fields: +0xe8 yaw, +0xec pitch (degrees), +0xf8 hardpoint offset, +0x104 hardpoint Matrix3f,
// +0xac -> +0x14 the entity (+0x58 Matrix43f); the port's are the same block at +0x1b8 (+0xd0), the entity
// [[w+0x160]+0x28], its matrix at +0x9c.
#include "lift/lift_rt13.h"
static uintptr_t scat_slide;
#define SCAT_WORLD (*(char **)(0x1038f5670UL + scat_slide))          // the port's global the original reads (rip + 0x1c5d4cc)
static float (*aim_rand_hook)(uint32_t);                         // the self-test's fakes; 0 in the game
static float (*aim_height_hook)(float, float, uint32_t);
static double LIFT_GETRAND(struct lift_cpu *c, uint32_t id) {
    (void)c; if (aim_rand_hook) return aim_rand_hook(id);
    return ((float (*)(void *, uint32_t))(0x10216d780UL + scat_slide))(SCAT_WORLD + 0x2b8, id);
}
static double LIFT_QTH(struct lift_cpu *c, float x, float z, uint32_t f) {
    (void)c; if (aim_height_hook) return aim_height_hook(x, z, f);
    return ((float (*)(void *, float, float, uint32_t))(0x1021a33d0UL + scat_slide))(*(void **)(SCAT_WORLD + 0x338), x, z, f);
}
#define LIFT_GLOB_13356a4 0x08000000u                                 // DOW2.exe's sim manager pointer: only its address is used
// DOW2.exe's static Matrix43f at 0x12be0b4 (the identity, in the image's data) that ComputeCoordinateSystem starts from
static const uint32_t aim_ident43[12] = { 0x3f800000u, 0, 0, 0, 0x3f800000u, 0, 0, 0, 0x3f800000u, 0, 0, 0 };
#define LIFT_GLOB_12be0b4 0x0a000000u
#define LIFT_GLOB_12be0bc 0x0a000008u
#define LIFT_GLOB_12be0c4 0x0a000010u
#define LIFT_GLOB_12be0cc 0x0a000018u
#define LIFT_GLOB_12be0d4 0x0a000020u
#define LIFT_GLOB_12be0dc 0x0a000028u
#include "lift/lift_aim.c"
struct aimctx { struct lift_cpu cpu; char w[0x130]; uint32_t hold[8]; uint32_t hw; };
static struct lift_cpu *aim_setup(struct aimctx *x, const char *mw) {
    struct lift_cpu *c = &x->cpu; memset(c, 0, offsetof(struct lift_cpu, base)); lift_init(c);
    memcpy(x->w + 0xe8, mw + 0x1b8, 0x40);
    char *ent = *(char **)(*(char *const *)(mw + 0x160) + 0x28);
    x->hw = lift_region(c, 2, x->w);
    uint32_t he = lift_region(c, 3, ent + 0x9c - 0x58), hh = lift_region(c, 4, x->hold);
    x->hold[5] = he; memcpy(x->w + 0xac, &hh, 4);
    return c;
}
#define AIM_PUSH(v) do { c->esp -= 4; S32(c->esp, (v)); } while (0)
// void ShootingWeapon::GetWorldPositionHeading(Vector3f &pos, Vector3f &heading, Vector3f &right) const
__attribute__((used)) void aimfix_worldposheading(const char *mw, float *pos, float *head, float *right) {
    struct aimctx x; struct lift_cpu *c = aim_setup(&x, mw);
    c->esi = lift_region(c, 6, head); c->edi = lift_region(c, 7, right);
    AIM_PUSH(lift_region(c, 5, pos)); AIM_PUSH(x.hw); AIM_PUSH(0xdeadbeefu);
    dw_a49d10(c);
}
// bool ShootingWeapon::GetAnglesToTarget(const Vector3f &target, float &yaw, float &pitch) const
__attribute__((used)) char aimfix_anglestotarget(const char *mw, const float *tgt, float *a1, float *a2) {
    struct aimctx x; struct lift_cpu *c = aim_setup(&x, mw);
    c->ecx = x.hw;
    AIM_PUSH(lift_region(c, 7, a2)); AIM_PUSH(lift_region(c, 6, a1)); AIM_PUSH(lift_region(c, 5, (void *)tgt)); AIM_PUSH(0xdeadbeefu);
    dw_a49450(c);
    return (char)(c->eax & 0xff);
}
// void ShootingWeapon::ConvertWorldSpacePointToWeaponSpace(const Vector3f &in, Vector3f &out) const
__attribute__((used)) float *aimfix_toweaponspace(const char *mw, const float *in, float *out) {
    struct aimctx x; struct lift_cpu *c = aim_setup(&x, mw);
    float tmp[3]; memcpy(tmp, in, 12);
    c->edx = lift_region(c, 5, tmp); c->eax = lift_region(c, 6, out);
    AIM_PUSH(x.hw); AIM_PUSH(0xdeadbeefu);
    dw_a49910(c);
    return out;
}

// WeaponUtil::CalculateScatterPosition run as DOW2.exe's own code (0x98d860 with 0x98d710 the scatter
// ranges, 0x676730 rotate about an axis, 0x6757c0/e0 cosf/sinf; lift/lift_scatter.c). Windows mixes SSE and x87 (at 24-bit
// precision) here and adds its vector * matrix sums in its own order; the port differs in the last bits (and so in where
// the shot lands). The random numbers come from the port's own SimManager::GetRand (same generator, randfix), the
// terrain height from its CollisionSystem::QueryTerrainHeight, in Windows' order of calls.
// Windows: target in ecx; info, flag, from, int *counter, bool *out_flag, Vector3f *out, b6, b7 on the stack.
// The port: (info, flag, from, target, counter, out_flag, out, b6, b7). The info block has the same layout on both sides.
__attribute__((used)) char aimfix_scatter(const char *info, char flag, const float *from, const float *tgt, int32_t *counter,
                                          char *oflag, float *out, char b6, char b7) {
    struct lift_cpu cpu, *c = &cpu; memset(c, 0, offsetof(struct lift_cpu, base)); lift_init(c);
    uint32_t glob[1] = { 0x09000000u };
    lift_region(c, 8, glob);
    c->ecx = lift_region(c, 4, (void *)tgt);
    uint32_t a[8] = { lift_region(c, 2, (void *)info), (uint8_t)flag, lift_region(c, 3, (void *)from), lift_region(c, 5, counter),
                      lift_region(c, 6, oflag), lift_region(c, 7, out), (uint8_t)b6, (uint8_t)b7 };
    for (int i = 7; i >= 0; i--) { c->esp -= 4; S32(c->esp, a[i]); }
    c->esp -= 4; S32(c->esp, 0xdeadbeefu);
    dw_98d860(c);
    return (char)(c->eax & 0xff);
}

// void ShootingWeapon::ComputeCoordinateSystem() (Windows 0xa49530, called once by the constructor): the hardpoint's offset
// (+0x1c8) and rotation (+0x1d4) from its position / direction (the port's +0x58 / +0x64 = Windows' +0x2c / +0x38). Every
// aim computation starts from these.
__attribute__((used)) void aimfix_coordsys(char *mw) {
    struct lift_cpu cpu, *c = &cpu; memset(c, 0, offsetof(struct lift_cpu, base)); lift_init(c);
    char w[0x130]; memset(w, 0, sizeof w); memcpy(w + 0x2c, mw + 0x58, 0x18);
    lift_region(c, 10, (void *)aim_ident43);
    c->ecx = lift_region(c, 2, w);
    c->esp -= 4; S32(c->esp, 0xdeadbeefu);
    dw_a49530(c);
    memcpy(mw + 0x1c8, w + 0xf8, 0x30);
}
// bool MathCollision::FindIntersection(const OBBf &box, const Rayf &ray, Vector3f &hit) (the port 0x10193b290, DOW2.exe's
// copy 0x679e50: eax = box, ecx = ray, hit on the stack): where a shot meets the target's box (GetHitPositionOnEntity)
__attribute__((used)) char aimfix_rayobb(const float *obb, const float *ray, float *hit) {
    struct lift_cpu cpu, *c = &cpu; memset(c, 0, offsetof(struct lift_cpu, base)); lift_init(c);
    c->eax = lift_region(c, 2, (void *)obb); c->ecx = lift_region(c, 3, (void *)ray);
    c->esp -= 4; S32(c->esp, lift_region(c, 4, hit)); c->esp -= 4; S32(c->esp, 0xdeadbeefu);
    dw_679e50(c);
    return (char)(c->eax & 0xff);
}
