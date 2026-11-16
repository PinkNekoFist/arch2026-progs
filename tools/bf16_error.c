/* Host-side error measurement for the BF16 routines of src/bf16.
 *
 *   cc -O2 -o build/bf16_error tools/bf16_error.c src/bf16/bf16.c -lm
 *   build/bf16_error
 *
 * Runs the C version of each routine over every input of the class below and
 * compares it with the exact result in double precision, which holds every
 * BF16 value and every product of two BF16 values exactly. The
 * assembly returns the same bit patterns (tests/tbf16.c checks both), so the
 * figures hold for it too. Errors are in ulps of the result: one ulp of a
 * normal BF16 with biased exponent e is 2^(e - 127 - 7).
 *
 *   mul       all pairs of positive finite operands with a normal result
 *   sqrt      all positive normals
 *
 * bf16_from_f32 is not measured: its rounding is exact by construction, with
 * an error of at most half an ulp (proved in the report).
 *
 * Signs only flip the result, so positive inputs cover all cases. The whole
 * run takes about 5 seconds.
 */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../src/bf16/bf16.h"

static double from_bf16(uint16_t h)
{
    uint32_t u = (uint32_t) h << 16;
    float f;
    memcpy(&f, &u, sizeof f);
    return f;
}

/* One ulp of a BF16 with biased exponent e; subnormals share e = 1. */
static double ulp(int e)
{
    return ldexp(1.0, (e ? e : 1) - 127 - 7);
}

static void measure_mul(void)
{
    double worst = 0, worst_rel = 0;
    uint16_t at_a = 0, at_b = 0;
    unsigned long long n = 0;
    for (uint32_t a = 1; a < 0x7F80; a++) {
        for (uint32_t b = 1; b < 0x7F80; b++) {
            uint16_t r = bf16_mul((uint16_t) a, (uint16_t) b);
            int e = (r >> 7) & 0xFF;
            if (e == 0 || e == 0xFF)
                continue; /* zero, subnormal or infinite result */
            double exact = from_bf16((uint16_t) a) * from_bf16((uint16_t) b);
            double diff = exact - from_bf16(r);
            if (diff < 0) {
                printf("mul: 0x%04X x 0x%04X rounded up\n", a, b);
                return;
            }
            double err = diff / ulp(e), rel = diff / exact;
            if (err > worst) {
                worst = err;
                at_a = (uint16_t) a;
                at_b = (uint16_t) b;
            }
            if (rel > worst_rel)
                worst_rel = rel;
            n++;
        }
    }
    printf(
        "mul: max %.6f ulp, %.4f%% relative, first at 0x%04X x 0x%04X, "
        "%llu normal results, none rounded up\n",
        worst, worst_rel * 100, at_a, at_b, n);
}

static void measure_sqrt(void)
{
    double worst_rel = 0, worst = 0;
    uint16_t at = 0;
    for (uint32_t a = 0x0080; a < 0x7F80; a++) {
        uint16_t r = bf16_sqrt((uint16_t) a);
        double exact = sqrt(from_bf16((uint16_t) a));
        double diff = fabs(from_bf16(r) - exact);
        double rel = diff / exact, err = diff / ulp((r >> 7) & 0xFF);
        if (rel > worst_rel) {
            worst_rel = rel;
            at = (uint16_t) a;
        }
        if (err > worst)
            worst = err;
    }
    printf("sqrt: max %.6f ulp, %.4f%% relative, first at 0x%04X\n", worst,
           worst_rel * 100, at);
}

int main(void)
{
    measure_mul();
    measure_sqrt();
    return 0;
}
