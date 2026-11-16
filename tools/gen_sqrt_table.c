/* Host-side generator for the square root table of src/bf16.
 *
 *   cc -O2 -o build/gen_sqrt_table tools/gen_sqrt_table.c
 *   build/gen_sqrt_table c      # body of sqrt_tab[256] in bf16.c
 *   build/gen_sqrt_table asm    # .byte lines of .Lsqrt_tab in bf16.S
 *
 * For a positive normal input a with biased exponent e and fraction f, the
 * result significand depends only on the low byte of a: bit 7 is the low bit
 * of e and bits 0-6 are f. Entry i of the table is computed as bf16.h defines
 * bf16_sqrt:
 *
 *   m = 128 + f, doubled when e is even (the unbiased exponent e - 127 is
 *   then odd and is made even);
 *   r = the largest mid in [90, 256] with (mid * mid) / 128 <= m, found by
 *   the same binary search;
 *   entry = r & 0x7F, the stored fraction (r is always in [128, 255]).
 *
 * The exponent of the result, ((e + 127) >> 1), is computed at run time.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* Largest mid in [90, 256] with (mid * mid) / 128 <= m. */
static uint32_t isqrt_search(uint32_t m)
{
    uint32_t low = 90, high = 256, result = 90;
    while (low <= high) {
        uint32_t mid = (low + high) / 2;
        if ((mid * mid) / 128 <= m) {
            result = mid;
            low = mid + 1;
        } else {
            high = mid - 1;
        }
    }
    return result;
}

static uint8_t entry(uint32_t i)
{
    uint32_t odd_e = i >> 7, f = i & 0x7F;
    uint32_t m = 128 + f;
    if (!odd_e)
        m <<= 1;
    uint32_t r = isqrt_search(m);
    if (r < 128 || r > 255) {
        fprintf(stderr, "entry %u: r = %u out of [128, 255]\n", i, r);
        r = 128;
    }
    return (uint8_t) (r & 0x7F);
}

int main(int argc, char **argv)
{
    int asm_out;
    if (argc == 2 && !strcmp(argv[1], "c")) {
        asm_out = 0;
    } else if (argc == 2 && !strcmp(argv[1], "asm")) {
        asm_out = 1;
    } else {
        fprintf(stderr, "usage: %s c|asm\n", argv[0]);
        return 1;
    }

    /* 12 per line matches clang-format in bf16.c, 8 per line bf16.S. */
    uint32_t per_line = asm_out ? 8 : 12;
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t col = i % per_line;
        if (col == 0)
            fputs(asm_out ? "    .byte   " : "   ", stdout);
        if (asm_out)
            printf("%s0x%02x", col ? ", " : "", entry(i));
        else
            printf(" 0x%02x,", entry(i));
        if (col == per_line - 1 || i == 255)
            putchar('\n');
    }
    return 0;
}
