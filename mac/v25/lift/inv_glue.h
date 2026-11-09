// inv_glue.h - Math::Matrix43f::InverseSelf as SimEngine.dll does it (0x1004e3a0, matrix in eax, in place; lifted by lift.py)
// for the Mac callers whose Windows counterparts are in SimEngine.dll (CollisionModel::SetTransform / GetOBB2f,
// CollisionSystem::QueryEntities)
#include "lift_rt.h"
#include "lift_inv.c"
__attribute__((used)) float *combatfix_inverse_se(float *m) {
    struct lift_cpu cpu, *c = &cpu; memset(c, 0, offsetof(struct lift_cpu, base)); lift_init(c);
    c->eax = lift_region(c, 2, m);
    c->esp -= 4; S32(c->esp, 0xdeadbeefu);
    se_1004e3a0(c);
    return m;
}
