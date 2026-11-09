__attribute__((used)) float *sumfix_vm3_yz_xy_xy(float *v, const float *m) {
    float x = v[0], y = v[1], z = v[2];
    float r0 = (y * m[3] + z * m[6]) + x * m[0];
    float r1 = (x * m[1] + y * m[4]) + z * m[7];
    float r2 = (x * m[2] + y * m[5]) + z * m[8];
    v[0] = r0; v[1] = r1; v[2] = r2; return v;
}
__attribute__((used)) float *sumfix_vm43_yz_xy_xy(float *v, const float *m) {
    float x = v[0], y = v[1], z = v[2];
    float r0 = ((y * m[3] + z * m[6]) + x * m[0]) + m[9];
    float r1 = ((x * m[1] + y * m[4]) + z * m[7]) + m[10];
    float r2 = ((x * m[2] + y * m[5]) + z * m[8]) + m[11];
    v[0] = r0; v[1] = r1; v[2] = r2; return v;
}
__attribute__((used)) float *sumfix_vm43_xz_xz_xz(float *v, const float *m) {
    float x = v[0], y = v[1], z = v[2];
    float r0 = ((x * m[0] + z * m[6]) + y * m[3]) + m[9];
    float r1 = ((x * m[1] + z * m[7]) + y * m[4]) + m[10];
    float r2 = ((x * m[2] + z * m[8]) + y * m[5]) + m[11];
    v[0] = r0; v[1] = r1; v[2] = r2; return v;
}
__attribute__((used)) float *sumfix_vm43_yz_xz_xz(float *v, const float *m) {
    float x = v[0], y = v[1], z = v[2];
    float r0 = ((y * m[3] + z * m[6]) + x * m[0]) + m[9];
    float r1 = ((x * m[1] + z * m[7]) + y * m[4]) + m[10];
    float r2 = ((x * m[2] + z * m[8]) + y * m[5]) + m[11];
    v[0] = r0; v[1] = r1; v[2] = r2; return v;
}
__attribute__((used)) float *sumfix_vm43_yz_xy_yz(float *v, const float *m) {
    float x = v[0], y = v[1], z = v[2];
    float r0 = ((y * m[3] + z * m[6]) + x * m[0]) + m[9];
    float r1 = ((x * m[1] + y * m[4]) + z * m[7]) + m[10];
    float r2 = ((y * m[5] + z * m[8]) + x * m[2]) + m[11];
    v[0] = r0; v[1] = r1; v[2] = r2; return v;
}
static void *const SF_HELPERS[] = { (void *)sumfix_vm3_yz_xy_xy, (void *)sumfix_vm43_yz_xy_xy, (void *)sumfix_vm43_xz_xz_xz, (void *)sumfix_vm43_yz_xz_xz, (void *)sumfix_vm43_yz_xy_yz };
static const struct sf_call { const char *name; uint64_t at, target; int helper; } SF_CALLS[] = {
    { "WeaponUtil::GetHitPositionOnEntity random point *= rotation (Win 0x98d4e6)", 0x101c97d6bULL, 0x101945530ULL, 0 },
    { "ModProjectileDynamics::updatePositionOnTarget offset *= target matrix (Win 0x99ab14)", 0x101d44b6bULL, 0x1019455c0ULL, 1 },
    { "ModProjectileDynamics::RequestMove target *= inverse(target matrix) (Win 0x998884)", 0x101d4399dULL, 0x1019455c0ULL, 2 },
    { "HoldSlot::GetPosition slot *= building matrix (Win 0xa29bba)", 0x101cf9a56ULL, 0x1019455c0ULL, 3 },
    { "ConstructionExt::ReserveConstructionSpot 1 (Win 0x899efe/0x899f21, x and z used)", 0x101b56720ULL, 0x1019455c0ULL, 4 },
    { "ConstructionExt::ReserveConstructionSpot 2 (Win 0x89a037/0x89a05a, x and z used)", 0x101b567cdULL, 0x1019455c0ULL, 4 },
};
#define SF_NCALLS ((int)(sizeof SF_CALLS / sizeof SF_CALLS[0]))
