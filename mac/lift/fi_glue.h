// fi_glue.h - MathCollision::FindIntersection (Mac ABI: obb, segment, int&, CollisionPoint*, options) run as SimEngine.dll's
// own code (0x10051890, lifted to C by lift.py; Windows has the options of its only caller, QueryEntities, folded in)
#include "lift_rt.h"
#include "lift_fi.c"
__attribute__((used)) char combatfix_findintersection(const float *obb, const float *seg, int32_t *n, void *cp, int opts) {
    struct lift_cpu cpu, *c = &cpu; memset(c, 0, offsetof(struct lift_cpu, base)); lift_init(c);
    uint32_t ho = lift_region(c, 2, (void *)obb), hs = lift_region(c, 3, (void *)seg), hn = lift_region(c, 4, n), hc = lift_region(c, 5, cp);
    c->esp -= 4; S32(c->esp, hn); c->esp -= 4; S32(c->esp, hs); c->esp -= 4; S32(c->esp, ho);
    c->esp -= 4; S32(c->esp, 0xdeadbeefu); c->ecx = hc;
    se_10051890(c);
    return (char)(c->eax & 0xff);
}
