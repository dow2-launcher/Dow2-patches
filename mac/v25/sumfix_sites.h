// sumfix_sites.h - sumfix's in-place edits (generated part: sumfix_sites.inc by gen_sum.py)
#include <stdint.h>
static const struct sf_site { const char *name; uint64_t addr; int len; unsigned char want[48], put[48]; } SF_SITES[] = {
#include "sumfix_sites.inc"
};
#define SF_NSITES ((int)(sizeof SF_SITES / sizeof SF_SITES[0]))
#include "sumfix_helpers.inc"
// divisions by a constant -> multiplications by Windows' reciprocal (sumfix_divs.inc from gen_sum.py): the divss's 0x5E opcode
// byte becomes 0x59 (mulss) and its rip-relative operand points at the reciprocal, kept in the near page
static const struct sf_div { const char *name; uint64_t addr; int len; unsigned char want[10]; int opo, dispo; uint32_t recip; } SF_DIVS[] = {
#include "sumfix_divs.inc"
};
#define SF_NDIVS ((int)(sizeof SF_DIVS / sizeof SF_DIVS[0]))
// stubs (sumfix_stubs.inc): the region becomes "jmp rel32 -> stub" + nops; the stub (near page, 0x400 + 0x60 * i) holds the
// Windows-order code and returns with "jmp [rip+0]; .quad back" (back = region end, written at blob + backoff)
static const struct sf_stub { const char *name; uint64_t addr; int len; unsigned char want[64]; int bloblen; unsigned char blob[96]; int backoff; } SF_STUBS[] = {
#include "sumfix_stubs.inc"
};
#define SF_NSTUBS ((int)(sizeof SF_STUBS / sizeof SF_STUBS[0]))
