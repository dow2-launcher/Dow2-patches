// combatfix_helpers.h - Math helpers in the order Windows' inlined copies add (-ffp-contract=off, no fast-math), and the
// SimEngine.dll functions lifted to C (lift/*.h); CF_HELPERS[i] is what CF_CALLS' helper index i calls
#include <stdint.h>
// Vector3f *= Matrix43f as inlined in DOW2.exe's doCollision (0x99a2f4); m = rows X, Y, Z, P
__attribute__((used)) float *combatfix_vecmat_docollision(float *v, const float *m) {
    float x = v[0], y = v[1], z = v[2];
    float rx = ((z * m[6] + y * m[3]) + x * m[0]) + m[9];
    float ry = ((x * m[1] + z * m[7]) + y * m[4]) + m[10];
    float rz = ((x * m[2] + z * m[8]) + y * m[5]) + m[11];
    v[0] = rx; v[1] = ry; v[2] = rz;
    return v;
}
// Vector3f *= Matrix43f as SimEngine.dll's CollisionSystem::QueryEntities inlines it (0x1004a150, 0x1004a32b)
__attribute__((used)) float *combatfix_vecmat43_zyx(float *v, const float *m) {
    float x = v[0], y = v[1], z = v[2];
    float rx = ((z * m[6] + y * m[3]) + x * m[0]) + m[9];
    float ry = ((z * m[7] + y * m[4]) + x * m[1]) + m[10];
    float rz = ((z * m[8] + y * m[5]) + x * m[2]) + m[11];
    v[0] = rx; v[1] = ry; v[2] = rz;
    return v;
}
// Vector3f *= Matrix3f, the same order (QueryEntities' segment direction, 0x1004a234)
__attribute__((used)) float *combatfix_vecmat3_zyx(float *v, const float *m) {
    float x = v[0], y = v[1], z = v[2];
    float rx = (z * m[6] + y * m[3]) + x * m[0];
    float ry = (z * m[7] + y * m[4]) + x * m[1];
    float rz = (z * m[8] + y * m[5]) + x * m[2];
    v[0] = rx; v[1] = ry; v[2] = rz;
    return v;
}
// Vector3f *= Matrix3f as SimEngine.dll's Mesh::TransformMesh inlines it (0x1004f693; the port adds the translation after)
__attribute__((used)) float *combatfix_vecmat3_mesh(float *v, const float *m) {
    float x = v[0], y = v[1], z = v[2];
    float rx = (z * m[6] + y * m[3]) + x * m[0];
    float ry = (x * m[1] + z * m[7]) + y * m[4];
    float rz = (x * m[2] + z * m[8]) + y * m[5];
    v[0] = rx; v[1] = ry; v[2] = rz;
    return v;
}
#include "lift/inv_glue.h"
#include "lift/fi_glue.h"
#include "lift/tr_glue.h"
static void *const CF_HELPERS[] = { (void *)combatfix_vecmat_docollision, (void *)combatfix_vecmat43_zyx, (void *)combatfix_vecmat3_zyx,
                                    (void *)combatfix_vecmat3_mesh, (void *)combatfix_inverse_se, (void *)combatfix_findintersection,
                                    (void *)combatfix_segtri };
