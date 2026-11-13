// obb_glue.h - OBB2 distance math run as SimEngine.dll's own code (lift/lift_obb.c from 0x1004ec10 DistanceSquared(OBB2, OBB2)
// with its corner / segment helpers 0x1004ea90 0x1004ef30 0x100505e0, 0x1001e2f0 EntityUtil::IsWithinDist(Entity, Entity, d),
// 0x1004f560 IsWithinDistShifted(OBB2 at a point, OBB2, d)). DOW2.exe's statically linked copy of DistanceSquared
// (0x67e880) is the same instruction stream, so this serves the port's callers from both sides.
// The port: DistanceSquared differs in 25% of random cases; the range test adds the 5 terms of the bound in another order
// (Windows ((((A.ey + A.ex) + B.ey) + B.ex) + d), the port ((((A.ex + A.ey) + B.ex) + B.ey) + d)).
#include "lift/lift_rt13.h"
#include "lift/lift_obb.c"
#define OBB_INIT struct lift_cpu cpu, *c = &cpu; memset(c, 0, offsetof(struct lift_cpu, base)); lift_init(c)
// float MathCollision::DistanceSquared(const OBB2f &a, const OBB2f &b)
__attribute__((used)) float rangefix_distsq(const float *a, const float *b) {
    OBB_INIT;
    c->ecx = lift_region(c, 2, (void *)a); c->eax = lift_region(c, 3, (void *)b);
    c->esp -= 4; S32(c->esp, 0xdeadbeefu);
    se_1004ec10(c);
    return c->x[0].f[0];
}
// bool MathCollision::IsWithinDist(const OBB2f &a, const OBB2f &b, float d): SimEngine's EntityUtil::IsWithinDist reads the
// entities' OBB2 at +0xe0, so the "entities" are regions based 0xe0 below the boxes
__attribute__((used)) char rangefix_iswithin(const float *a, const float *b, float d) {
    OBB_INIT;
    uint32_t ha = lift_region(c, 2, (char *)a - 0xe0), hb = lift_region(c, 3, (char *)b - 0xe0), dd; memcpy(&dd, &d, 4);
    c->esp -= 4; S32(c->esp, dd); c->esp -= 4; S32(c->esp, hb); c->esp -= 4; S32(c->esp, ha); c->esp -= 4; S32(c->esp, 0xdeadbeefu);
    se_1001e2f0(c);
    return (char)(c->eax & 0xff);
}
// bool MathCollision::IsWithinDistShifted(const OBB2f &a, const Vector2f &at, const OBB2f &b, float d)
__attribute__((used)) char rangefix_iswithinshifted(const float *a, const float *at, const float *b, float d) {
    OBB_INIT;
    uint32_t ha = lift_region(c, 2, (void *)a), dd; memcpy(&dd, &d, 4);
    c->eax = lift_region(c, 3, (void *)b); c->edx = lift_region(c, 4, (void *)at);
    c->esp -= 4; S32(c->esp, dd); c->esp -= 4; S32(c->esp, ha); c->esp -= 4; S32(c->esp, 0xdeadbeefu);
    se_1004f560(c);
    return (char)(c->eax & 0xff);
}
