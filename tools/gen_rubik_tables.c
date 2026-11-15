/* Host-side generator for the 2x2x2 solver tables of src/rubik.
 *
 *   cc -O2 -o build/gen_rubik_tables tools/gen_rubik_tables.c
 *   build/gen_rubik_tables src/rubik/rubik_tables.h src/rubik/rubik_tables.S
 *
 * One breadth-first search over all 3,674,160 states gives the exact distance
 * of every state. From it come four tables, written once for the C version
 * (static arrays in a header) and once for the assembly (.rodata):
 *
 *   o_pdb[729]    distance to solve the orientations alone
 *   p_pdb[5040]   distance to solve the permutation alone
 *   cache5[]      every state within 5 moves, sorted by rank, as
 *                 rank << 7 | distance << 4 | next move
 *   d11[]         every state at distance 11, sorted by rank, as
 *                 rank << 4 | next move
 *
 * The assembly also gets o_move[729][9], the orientation rank after each
 * move, as orientations move independently of the permutation.
 *
 * The next move of a state at distance d leads to a state at distance d - 1.
 * The solved state has no next move and gets 0xF instead, so that no entry
 * is 0 and a lookup can use 0 for "absent".
 * The rank is p_rank * 729 + o_rank, with o_rank the base-3 number of the
 * first six orientations and p_rank the Lehmer code of the permutation in
 * the factorial base (factors 6, 5, 4, 3, 2 after the first digit), exactly as
 * src/rubik/rubik.c and rubik.S compute it.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    CUBIES = 7,
    O_STATES = 729,
    P_STATES = 5040,
    N_STATES = O_STATES * P_STATES,
    CACHE_DEPTH = 5,
    DIAMETER = 11,
};

struct cube {
    uint8_t p[CUBIES], o[CUBIES];
};

/* The model of sysprog21/minirubik solver.c: moves R R2 R' B B2 B' D D2 D'. */
static const uint8_t move_p[9][CUBIES] = {
    {1, 4, 2, 0, 3, 5, 6}, {4, 3, 2, 1, 0, 5, 6}, {3, 0, 2, 4, 1, 5, 6},
    {0, 1, 2, 4, 5, 6, 3}, {0, 1, 2, 5, 6, 3, 4}, {0, 1, 2, 6, 3, 4, 5},
    {0, 2, 5, 3, 1, 4, 6}, {0, 5, 4, 3, 2, 1, 6}, {0, 4, 1, 3, 5, 2, 6},
};
static const uint8_t move_o[9][CUBIES] = {
    {1, 2, 0, 2, 1, 0, 0}, {0, 0, 0, 0, 0, 0, 0}, {1, 2, 0, 2, 1, 0, 0},
    {0, 0, 0, 1, 2, 1, 2}, {0, 0, 0, 0, 0, 0, 0}, {0, 0, 0, 1, 2, 1, 2},
    {0, 0, 0, 0, 0, 0, 0}, {0, 0, 0, 0, 0, 0, 0}, {0, 0, 0, 0, 0, 0, 0},
};

static struct cube apply(struct cube s, int m)
{
    struct cube t;
    for (int i = 0; i < CUBIES; i++) {
        int j = move_p[m][i];
        t.p[i] = s.p[j];
        t.o[i] = (uint8_t) ((s.o[j] + move_o[m][i]) % 3);
    }
    return t;
}

static uint32_t o_rank(const struct cube *s)
{
    uint32_t r = 0;
    for (int i = 0; i < CUBIES - 1; i++)
        r = r * 3 + s->o[i];
    return r;
}

static uint32_t p_rank(const struct cube *s)
{
    uint32_t r = 0;
    for (int i = 0; i < CUBIES - 1; i++) {
        uint32_t n = 0;
        for (int j = i + 1; j < CUBIES; j++)
            n += s->p[j] < s->p[i];
        r = r * (uint32_t) (CUBIES - i) + n;
    }
    return r;
}

static uint32_t rank_of(const struct cube *s)
{
    return p_rank(s) * O_STATES + o_rank(s);
}

static uint8_t dist[N_STATES];
static struct cube queue[N_STATES];
static uint8_t o_pdb[O_STATES], p_pdb[P_STATES];
static uint16_t o_move[O_STATES * 9];
static uint32_t cache5[N_STATES], d11[N_STATES];

static void bfs(void)
{
    memset(dist, 0xFF, sizeof dist);
    memset(o_pdb, 0xFF, sizeof o_pdb);
    memset(p_pdb, 0xFF, sizeof p_pdb);
    struct cube solved;
    for (int i = 0; i < CUBIES; i++) {
        solved.p[i] = (uint8_t) i;
        solved.o[i] = 0;
    }
    size_t head = 0, tail = 0;
    queue[tail++] = solved;
    dist[rank_of(&solved)] = 0;
    while (head < tail) {
        struct cube s = queue[head++];
        uint8_t d = dist[rank_of(&s)];
        /* Breadth-first order makes the first visit of each projection the
         * shortest distance in the quotient graph as well.
         */
        if (o_pdb[o_rank(&s)] == 0xFF)
            o_pdb[o_rank(&s)] = d;
        if (p_pdb[p_rank(&s)] == 0xFF)
            p_pdb[p_rank(&s)] = d;
        for (int m = 0; m < 9; m++) {
            struct cube t = apply(s, m);
            uint32_t r = rank_of(&t);
            if (dist[r] == 0xFF) {
                dist[r] = (uint8_t) (d + 1);
                queue[tail++] = t;
            }
        }
    }
    if (tail != N_STATES) {
        fprintf(stderr, "reached %zu states, expected %d\n", tail, N_STATES);
        exit(1);
    }
}

/* The first move, in move order, that brings s one step closer to solved. */
static int next_move(const struct cube *s)
{
    int d = dist[rank_of(s)];
    for (int m = 0; m < 9; m++) {
        struct cube t = apply(*s, m);
        if (dist[rank_of(&t)] == d - 1)
            return m;
    }
    fprintf(stderr, "no move toward solved\n");
    exit(1);
}

static void build(size_t *n5, size_t *n11)
{
    /* Walk the queue, which holds every state once, and file by rank. */
    static uint8_t mark[N_STATES];
    for (size_t i = 0; i < N_STATES; i++) {
        const struct cube *s = &queue[i];
        uint32_t r = rank_of(s);
        int d = dist[r];
        if (d <= CACHE_DEPTH || d == DIAMETER)
            mark[r] = (uint8_t) (0x80 | (d == 0 ? 0xF : next_move(s)));
    }
    *n5 = *n11 = 0;
    for (uint32_t r = 0; r < N_STATES; r++) {
        if (!mark[r])
            continue;
        uint32_t m = mark[r] & 0xF;
        if (dist[r] == DIAMETER)
            d11[(*n11)++] = r << 4 | m;
        else
            cache5[(*n5)++] = r << 7 | (uint32_t) dist[r] << 4 | m;
    }
}

/* The orientation rank after each move, from the six stored orientations; the
 * seventh makes the sum a multiple of 3.
 */
static void build_o_move(void)
{
    for (uint32_t r = 0; r < O_STATES; r++) {
        struct cube s;
        uint32_t x = r, sum = 0;
        for (int i = CUBIES - 2; i >= 0; i--) {
            s.o[i] = (uint8_t) (x % 3);
            x /= 3;
            sum += s.o[i];
        }
        s.o[CUBIES - 1] = (uint8_t) ((3 - sum % 3) % 3);
        for (int i = 0; i < CUBIES; i++)
            s.p[i] = (uint8_t) i;
        if (o_rank(&s) != r) {
            fprintf(stderr, "orientation rank mismatch\n");
            exit(1);
        }
        for (int m = 0; m < 9; m++) {
            struct cube t = apply(s, m);
            o_move[r * 9 + (uint32_t) m] = (uint16_t) o_rank(&t);
        }
    }
}

static void emit_c_u8(FILE *f, const char *name, const uint8_t *v, size_t n)
{
    fprintf(f, "static const uint8_t %s[%zu] = {", name, n);
    for (size_t i = 0; i < n; i++)
        fprintf(f, "%s%u,", i % 12 ? " " : "\n    ", v[i]);
    fprintf(f, "\n};\n\n");
}

static void emit_c_u32(FILE *f, const char *name, const uint32_t *v, size_t n)
{
    fprintf(f, "static const uint32_t %s[%zu] = {", name, n);
    for (size_t i = 0; i < n; i++)
        fprintf(f, "%s0x%08X,", i % 6 ? " " : "\n    ", v[i]);
    fprintf(f, "\n};\n\n");
}

/* width: 1 for .byte, 2 for .half, 4 for .word */
static void emit_s(FILE *f,
                   const char *name,
                   const void *v,
                   size_t n,
                   int width)
{
    fprintf(f, "    .globl  %s\n    .type   %s, @object\n", name, name);
    fprintf(f, "    .align  2\n%s:", name);
    for (size_t i = 0; i < n; i++) {
        if (width == 4)
            fprintf(f, "%s0x%08X", i % 6 ? ", " : "\n    .word   ",
                    ((const uint32_t *) v)[i]);
        else if (width == 2)
            fprintf(f, "%s%u", i % 9 ? ", " : "\n    .half   ",
                    ((const uint16_t *) v)[i]);
        else
            fprintf(f, "%s%u", i % 16 ? ", " : "\n    .byte   ",
                    ((const uint8_t *) v)[i]);
    }
    fprintf(f, "\n    .size   %s, .-%s\n\n", name, name);
}

int main(int argc, char **argv)
{
    if (argc != 3) {
        fprintf(stderr, "usage: %s tables.h tables.S\n", argv[0]);
        return 2;
    }
    bfs();
    size_t n5, n11;
    build(&n5, &n11);
    build_o_move();

    FILE *h = fopen(argv[1], "w");
    if (!h) {
        perror(argv[1]);
        return 1;
    }
    fprintf(h,
            "/* Generated by tools/gen_rubik_tables.c. Do not edit. */\n"
            "#pragma once\n\n#include <stdint.h>\n\n"
            "#define RUBIK_CACHE5_N %zu\n#define RUBIK_D11_N %zu\n\n"
            "/* clang-format off */\n",
            n5, n11);
    emit_c_u8(h, "rubik_o_pdb", o_pdb, O_STATES);
    emit_c_u8(h, "rubik_p_pdb", p_pdb, P_STATES);
    emit_c_u32(h, "rubik_cache5", cache5, n5);
    emit_c_u32(h, "rubik_d11", d11, n11);
    fprintf(h, "/* clang-format on */\n");
    fclose(h);

    FILE *s = fopen(argv[2], "w");
    if (!s) {
        perror(argv[2]);
        return 1;
    }
    fprintf(s,
            "# Generated by tools/gen_rubik_tables.c. Do not edit.\n"
            "# Tables for rubik_solve_asm: the data of rubik_tables.h and the\n"
            "# orientation move table o_move[729][9].\n\n"
            "    .section .rodata\n\n");
    emit_s(s, "rubik_asm_o_pdb", o_pdb, O_STATES, 1);
    emit_s(s, "rubik_asm_p_pdb", p_pdb, P_STATES, 1);
    emit_s(s, "rubik_asm_cache5", cache5, n5, 4);
    emit_s(s, "rubik_asm_d11", d11, n11, 4);
    emit_s(s, "rubik_asm_o_move", o_move, O_STATES * 9, 2);
    fclose(s);

    fprintf(stderr, "cache5: %zu entries, d11: %zu entries\n", n5, n11);
    return 0;
}
