#include "rubik.h"
#include "rubik_tables.h"

/* Optimal 2x2x2 solver: iterative deepening A* over the nine moves, with
 * max(orientation distance, permutation distance) as the heuristic, cut short
 * by two tables (src/rubik/rubik_tables.h, from tools/gen_rubik_tables.c):
 *
 *   rubik_cache5  every state within 5 moves: exact distance and next move
 *   rubik_d11     every state at distance 11: next move
 *
 * A state missing from rubik_cache5 is at least 6 moves away, so the search
 * never has to go deeper than 4 moves before a lookup settles the rest, and a
 * state at distance 11 costs one move plus a search at a known threshold.
 * The target has no divider: no / or % on run-time values below.
 */

enum {
    CUBIES = 7,
    O_STATES = 729,
    CACHE_DEPTH = 5,
    DIAMETER = 11,
    NO_FACE = 3,
};

struct cube {
    uint8_t p[CUBIES], o[CUBIES];
};

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

static void apply(const struct cube *s, int m, struct cube *t)
{
    for (int i = 0; i < CUBIES; i++) {
        int j = move_p[m][i];
        t->p[i] = s->p[j];
        int o = s->o[j] + move_o[m][i];
        t->o[i] = (uint8_t) (o >= 3 ? o - 3 : o);
    }
}

static uint32_t o_rank(const struct cube *s)
{
    uint32_t r = 0;
    for (int i = 0; i < CUBIES - 1; i++)
        r = r * 3 + s->o[i];
    return r;
}

/* Lehmer code in the factorial base, written out so every factor is a
 * constant: digit i counts the later cubies smaller than cubie i.
 */
static uint32_t p_rank(const struct cube *s)
{
    uint32_t n[CUBIES - 1];
    for (int i = 0; i < CUBIES - 1; i++) {
        uint32_t c = 0;
        for (int j = i + 1; j < CUBIES; j++)
            c += s->p[j] < s->p[i];
        n[i] = c;
    }
    return ((((n[0] * 6 + n[1]) * 5 + n[2]) * 4 + n[3]) * 3 + n[4]) * 2 + n[5];
}

static uint32_t rank_of(const struct cube *s)
{
    return p_rank(s) * O_STATES + o_rank(s);
}

static int heuristic(const struct cube *s)
{
    int ho = rubik_o_pdb[o_rank(s)], hp = rubik_p_pdb[p_rank(s)];
    return ho > hp ? ho : hp;
}

/* Binary search of a table sorted by rank, the rank stored above shift bits.
 * Returns the entry, or 0 when the rank is absent; no entry is 0, as the
 * generator gives the solved state (rank 0) the unused move 0xF.
 */
static uint32_t lookup(const uint32_t *tab, int n, int shift, uint32_t rank)
{
    int lo = 0, hi = n - 1;
    while (lo <= hi) {
        int mid = (lo + hi) >> 1;
        uint32_t e = tab[mid], r = e >> shift;
        if (r == rank)
            return e;
        if (r < rank)
            lo = mid + 1;
        else
            hi = mid - 1;
    }
    return 0;
}

static uint32_t lookup5(const struct cube *s)
{
    return lookup(rubik_cache5, RUBIK_CACHE5_N, 7, rank_of(s));
}

/* Follow the next moves of rubik_cache5 from s for d steps. */
static void unpack(struct cube s, uint8_t *moves, int pos, int d)
{
    for (int k = 0; k < d; k++) {
        int m = (int) (lookup5(&s) & 0xF);
        struct cube t;
        moves[pos + k] = (uint8_t) m;
        apply(&s, m, &t);
        s = t;
    }
}

struct search {
    uint8_t *moves;
    int threshold, next;
};

static const uint8_t face_of[9] = {0, 0, 0, 1, 1, 1, 2, 2, 2};

/* Depth-first pass below s, which g moves have reached. Returns 1 when
 * moves[0 .. threshold) solves the cube; otherwise lowers ctx->next to the
 * smallest bound that exceeded the threshold.
 */
static int dfs(struct search *ctx, const struct cube *s, int g, int last_face)
{
    for (int m = 0; m < 9; m++) {
        int f = face_of[m];
        if (f == last_face)
            continue;
        struct cube t;
        apply(s, m, &t);
        int bound = g + 1 + heuristic(&t);
        if (bound > ctx->threshold) {
            if (bound < ctx->next)
                ctx->next = bound;
            continue;
        }
        ctx->moves[g] = (uint8_t) m;
        if (g + 1 + CACHE_DEPTH >= ctx->threshold) {
            /* Close enough for the cache to decide: a hit gives the exact
             * distance, a miss means at least CACHE_DEPTH + 1.
             */
            uint32_t e = lookup5(&t);
            if (e) {
                int d = (int) (e >> 4 & 7);
                bound = g + 1 + d;
                if (bound == ctx->threshold) {
                    unpack(t, ctx->moves, g + 1, d);
                    return 1;
                }
                if (bound > ctx->threshold && bound < ctx->next)
                    ctx->next = bound;
                continue;
            }
            bound = g + 1 + CACHE_DEPTH + 1;
            if (bound > ctx->threshold) {
                if (bound < ctx->next)
                    ctx->next = bound;
                continue;
            }
        }
        if (dfs(ctx, &t, g + 1, f))
            return 1;
    }
    return 0;
}

/* Solves s, which g moves already in moves[] have reached, starting with the
 * given threshold. Returns the solution length.
 */
static int ida(const struct cube *s, uint8_t *moves, int g, int threshold)
{
    int last_face = g ? face_of[moves[g - 1]] : NO_FACE;
    struct search ctx = {moves, threshold, 0};
    for (;;) {
        ctx.next = 99;
        if (dfs(&ctx, s, g, last_face))
            return ctx.threshold;
        ctx.threshold = ctx.next;
    }
}

static int solve(const struct cube *s, uint8_t *moves)
{
    int h = heuristic(s);
    if (h == 0)
        return 0;
    uint32_t e = lookup5(s);
    if (e) {
        int d = (int) (e >> 4 & 7);
        unpack(*s, moves, 0, d);
        return d;
    }
    e = lookup(rubik_d11, RUBIK_D11_N, 4, rank_of(s));
    if (e) {
        struct cube t;
        moves[0] = (uint8_t) (e & 0xF);
        apply(s, moves[0], &t);
        return ida(&t, moves, 1, DIAMETER);
    }
    return ida(s, moves, 0, h > CACHE_DEPTH + 1 ? h : CACHE_DEPTH + 1);
}

/* Validates the 14 characters; returns 0 and fills c, or -1. */
static int parse(const char *state, struct cube *c)
{
    unsigned seen = 0, sum = 0;
    for (int i = 0; i < RUBIK_STATE_LEN; i++) {
        if (!state[i])
            return -1;
    }
    if (state[RUBIK_STATE_LEN])
        return -1;
    for (int i = 0; i < CUBIES; i++) {
        unsigned p = (unsigned) (state[i] - '1');
        unsigned o = (unsigned) (state[i + CUBIES] - '1');
        if (p >= CUBIES || o >= 3 || (seen >> p & 1))
            return -1;
        seen |= 1u << p;
        sum += o;
        c->p[i] = (uint8_t) p;
        c->o[i] = (uint8_t) o;
    }
    while (sum >= 3)
        sum -= 3;
    return sum ? -1 : 0;
}

static const char face_name[3] = {'R', 'B', 'D'};
static const char turn_suffix[9] = {0, '2', '\'', 0, '2', '\'', 0, '2', '\''};

int rubik_solve(const char *state, char *out, size_t cap)
{
    struct cube c;
    if (parse(state, &c))
        return -1;

    uint8_t moves[DIAMETER];
    int n = solve(&c, moves);

    size_t need = 1;
    for (int i = 0; i < n; i++)
        need += (size_t) (i > 0) + 1 + (turn_suffix[moves[i]] != 0);
    if (need > cap) {
        if (cap)
            out[0] = '\0';
        return -2;
    }

    char *w = out;
    for (int i = 0; i < n; i++) {
        if (i)
            *w++ = ' ';
        *w++ = face_name[face_of[moves[i]]];
        if (turn_suffix[moves[i]])
            *w++ = turn_suffix[moves[i]];
    }
    *w = '\0';
    return n;
}
