// tr_glue.h - MathCollision::FindIntersection(Segmentf, Trianglef, Vector3f&, float*) as SimEngine.dll does it (0x10052000:
// eax = triangle, edx = point, esi = t, segment on the stack; lifted by lift.py), for CollisionModel::Query
#include "lift_rt.h"
#include "lift_tr.c"
__attribute__((used)) char combatfix_segtri(const float *seg, const float *tri, void *pt, void *t) {
    struct lift_cpu cpu, *c = &cpu; memset(c, 0, offsetof(struct lift_cpu, base)); lift_init(c);
    uint32_t hs = lift_region(c, 2, (void *)seg);
    c->eax = lift_region(c, 3, (void *)tri); c->edx = lift_region(c, 4, pt); c->esi = t ? lift_region(c, 5, t) : 0;
    c->esp -= 4; S32(c->esp, hs); c->esp -= 4; S32(c->esp, 0xdeadbeefu);
    se_10052000(c);
    return (char)(c->eax & 0xff);
}
