#include "fixed.h"

uint32_t op1(uint32_t a, uint32_t b, int *n)
{
    *n = 0;
    while (b != 0) {
        uint32_t c = (a & b) << 1;
        a = a ^ b;
        b = c;
        (*n)++;
    }
    return a;
}

uint32_t op2(uint32_t a, uint32_t b)
{
    return a - b;
}

uint32_t op3(uint32_t a, uint32_t b)
{
    return (a >> 1) + (b >> 1) + (b & a & 1);
}

int32_t q_mul(int32_t a, int32_t b)
{
    uint32_t ua = a < 0 ? 0u - (uint32_t) a : (uint32_t) a;
    uint32_t ub = b < 0 ? 0u - (uint32_t) b : (uint32_t) b;
    uint32_t lo = 0, hi = 0, alo = ua, ahi = 0;
    while (ub) {
        if (ub & 1) {
            lo += alo;
            hi += ahi + (lo < alo);
        }
        ahi = (ahi << 1) | (alo >> 31);
        alo <<= 1;
        ub >>= 1;
    }
    uint64_t acc = ((uint64_t) hi << 32) | lo;
    int64_t p = (a < 0) != (b < 0) ? -(int64_t) acc : (int64_t) acc;
    return (int32_t) ((p + (1 << 19)) >> 20);
}
