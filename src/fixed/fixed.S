# op1, op2, op3 and q_mul: hand-written RV32I.
#
# uint32_t op1_asm(uint32_t a, uint32_t b, int *n)   a0, a1, a2 -> a0
# uint32_t op2_asm(uint32_t a, uint32_t b)           a0, a1 -> a0
# uint32_t op3_asm(uint32_t a, uint32_t b)           a0, a1 -> a0
# int32_t  q_mul_asm(int32_t a, int32_t b)           a0, a1 -> a0
#
# RV32I and Zicsr only: no mul, div, rem, no floating point, no compressed
# instructions. q_mul forms a 64-bit product in a register pair. Tests:
# tests/tfixed.c.

    .text

    .globl  op1_asm
    .type   op1_asm, @function
    .align  2
op1_asm:
    # a3 c, a4 n
    li      a4, 0
    beqz    a1, .Lop1_end
.Lop1_loop:
    and     a3, a0, a1
    xor     a0, a0, a1
    slli    a1, a3, 1
    addi    a4, a4, 1
    bnez    a1, .Lop1_loop
.Lop1_end:
    sw      a4, 0(a2)
    ret
    .size   op1_asm, .-op1_asm

    .globl  op2_asm
    .type   op2_asm, @function
    .align  2
op2_asm:
    sub     a0, a0, a1
    ret
    .size   op2_asm, .-op2_asm

    .globl  op3_asm
    .type   op3_asm, @function
    .align  2
op3_asm:
    li      t0, 1
    and     t0, t0, a0
    and     t0, t0, a1

    srli    a0, a0, 1
    srli    a1, a1, 1
    add     a0, a0, a1
    add     a0, a0, t0
    ret
    .size   op3_asm, .-op3_asm

    .globl  q_mul_asm
    .type   q_mul_asm, @function
    .align  2
q_mul_asm:
    # t0 < 0 when result should be neg
    # a2 ua, a3 ub
    # a4 lo, a5 hi, a6 alo, a7 ahi
    xor     t0, a0, a1

    mv      a2, a0
    bgez    a0, .Lage0
    sub     a2, x0, a0
.Lage0:
    mv      a3, a1
    bgez    a1, .Lbge0
    sub     a3, x0, a1
.Lbge0:
    # decrease loop count by swap a, b
    bgeu    a2, a3, .Lno_swap
    mv      t2, a2
    mv      a2, a3
    mv      a3, t2
.Lno_swap:
    li      a4, 0
    li      a5, 0
    mv      a6, a2
    li      a7, 0
    beqz    a3, .Ladd_loop_end

.Ladd_loop:
    andi    t2, a3, 1
    beqz    t2, .Lbit0

    # if (ub & 1)
    add     a4, a4, a6
    sltu    t2, a4, a6
    add     a5, a5, a7
    add     a5, a5, t2

.Lbit0:
    srli    t2, a6, 31
    slli    a7, a7, 1
    or      a7, a7, t2
    slli    a6, a6, 1
    srli    a3, a3, 1
    bnez    a3, .Ladd_loop
.Ladd_loop_end:
    # signs differ
    bgez    t0, .Lis_pos
    snez    t2, a4
    sub     a4, x0, a4
    sub     a5, x0, a5
    sub     a5, a5, t2
.Lis_pos:
    # (p + 1 << 19) >> 20
    lui     t2, 0x80
    add     a4, a4, t2
    sltu    t2, a4, t2
    add     a5, a5, t2
    srli    a4, a4, 20
    slli    a5, a5, 12
    or      a0, a4, a5
    ret
    .size   q_mul_asm, .-q_mul_asm
