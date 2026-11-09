#include "gcd.h"

uint32_t gcd_u32(uint32_t a, uint32_t b)
{
    unsigned int c = 0, t_cnt = 0;
    while ((a | b) != 0) {
        if (a == 0 || b == 0) {
            c = (a | b);
            break;
        }

        if (((a | b) & 1U) == 0) {
            // both even
            t_cnt += 1;
        } else if (((a & b) & 1U) != 0) {
            // both odd
            if (a > b) {
                unsigned int d = a;
                a = b;
                b = d;
            }
            b -= a;
        }
        a >>= (a & 1U) ^ 1U;
        b >>= (b & 1U) ^ 1U;
    }
    return c << t_cnt;
}
