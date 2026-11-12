# gcd_u32_asm: binary GCD, hand-written RV32I.
#
# uint32_t gcd_u32_asm(uint32_t a, uint32_t b)   a in a0, b in a1, result in a0
#
# RV32I and Zicsr only: no mul, div, rem, no floating point, no compressed
# instructions. Tests: tests/tgcd.c.

    .text

    .globl  gcd_u32_asm
    .type   gcd_u32_asm, @function
    .align  2
gcd_u32_asm:
    li      t3, 0
.Lgcd_loop:
    # t1, t2, a, b, is odd
    andi    t1, a0, 1
    andi    t2, a1, 1

    beq     a0, x0, .Lgcd_loop_end
    beq     a1, x0, .Lgcd_loop_end

    # t0 a, b odd, even
    or      t0, t1, t2
    beq     t0, x0, .Lboth_even
    and     t0, t1, t2
    bne     t0, x0, .Lboth_odd

    beq     t2, x0, .Lb_even
.La_even:
    srli    a0, a0, 1
    j       .Lgcd_loop
.Lb_even:
    srli    a1, a1, 1
    j       .Lgcd_loop

    # t3, t_cnt
.Lboth_even:
    addi    t3, t3, 1
    srli    a0, a0, 1
    srli    a1, a1, 1
    j       .Lgcd_loop
.Lboth_odd:
    bgeu    a1, a0, .Lno_swap
    mv      t0, a0
    mv      a0, a1
    mv      a1, t0
.Lno_swap:
    sub     a1, a1, a0
    srli    a1, a1, 1
    j       .Lgcd_loop
.Lgcd_loop_end:
    or      a0, a0, a1
    sll     a0, a0, t3
    ret
    .size   gcd_u32_asm, .-gcd_u32_asm
