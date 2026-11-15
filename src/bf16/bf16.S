# BFloat16 routines: hand-written RV32I.
#
# uint16_t bf16_from_f32_asm(uint32_t f32bits)   a0 -> a0
# uint16_t bf16_mul_asm(uint16_t a, uint16_t b)  a0, a1 -> a0
# uint16_t bf16_sqrt_asm(uint16_t a)             a0 -> a0
#
# RV32I and Zicsr only: no mul, div, rem, no floating point, no compressed
# instructions. Tests: tests/tbf16.c.

    .text

    .globl  bf16_from_f32_asm
    .type   bf16_from_f32_asm, @function
    .align  2
bf16_from_f32_asm:
    lui     a2, 0x7F800
    and     a1, a0, a2
    bne     a1, a2, .Lfinite
    srli    a0, a0, 16
    ret
.Lfinite:
    srli    a1, a0, 16
    andi    a1, a1, 1
    li      a2, 0x7FFF
    add     a0, a0, a2
    add     a0, a0, a1
    srli    a0, a0, 16
    ret
    .size   bf16_from_f32_asm, .-bf16_from_f32_asm

    .globl  bf16_mul_asm
    .type   bf16_mul_asm, @function
    .align  2
bf16_mul_asm:
    # sign a2
    xor     a2, a0, a1
    srli    a2, a2, 15
    slli    a2, a2, 15
    # a3, aa, a4, bb
    li      t0, 0x7FFF
    and     a3, a0, t0
    and     a4, a1, t0

    # aa - 1 >= 0x7F7F
    li      t0, 0x7F7F
    addi    t1, a3, -1
    bgeu    t1, t0, .Lmul_special
    addi    t1, a4, -1
    bgeu    t1, t0, .Lmul_special

    # t1 ea, t2 eb, t3 ma, t4 mb
    srli    t1, a3, 7
    srli    t2, a4, 7
    andi    t3, a0, 0x7F
    andi    t4, a1, 0x7F

    # normalize
    beqz    t1, .Lmul_denorm_a
    ori     t3, t3, 0x80
.Lmul_norm_b:
    beqz    t2, .Lmul_denorm_b
    ori     t4, t4, 0x80
.Lmul_product:
    # a5 = ma * mb by shift-and-add;
    li      a5, 0
.Lmul_loop:
    andi    t5, t4, 1
    beqz    t5, .Lmul_skip
    add     a5, a5, t3
.Lmul_skip:
    slli    t3, t3, 1
    srli    t4, t4, 1
    bnez    t4, .Lmul_loop

    # exp = ea + eb - 127
    add     t1, t1, t2
    addi    t1, t1, -127

    # renormalize
    # otherwise p >>= 7
    srli    t5, a5, 15
    add     t1, t1, t5
    addi    t5, t5, 7
    srl     a5, a5, t5

    li      t5, 0xFF
    bge     t1, t5, .Lmul_inf
    blez    t1, .Lmul_denorm_out

    # normal result
    slli    t1, t1, 7
    andi    a5, a5, 0x7F
    or      a0, a2, t1
    or      a0, a0, a5
    ret

.Lmul_denorm_a:
    li      t1, 1
1:
    slli    t3, t3, 1
    addi    t1, t1, -1
    andi    t5, t3, 0x80
    beqz    t5, 1b
    j       .Lmul_norm_b

.Lmul_denorm_b:
    li      t2, 1
1:
    slli    t4, t4, 1
    addi    t2, t2, -1
    andi    t5, t4, 0x80
    beqz    t5, 1b
    j       .Lmul_product

.Lmul_denorm_out:
    # exp in [-6, 0]
    li      t5, -6
    blt     t1, t5, .Lmul_ret_sign
    andi    a5, a5, 0x7F
    li      t5, 1
    sub     t5, t5, t1
    srl     a5, a5, t5
    or      a0, a2, a5
    ret

.Lmul_special:
    li      t0, 0x7F80
    bltu    a3, t0, .Lmul_b_special
    bne     a3, t0, .Lmul_ret_a         # a is NaN
    beqz    a4, .Lmul_qnan              # Inf * 0
    or      a0, a2, t0                  # Inf with the product sign
    ret
.Lmul_b_special:
    bltu    a4, t0, .Lmul_ret_sign      # a or b is zero
    bne     a4, t0, .Lmul_ret_b         # b is NaN
    beqz    a3, .Lmul_qnan              # 0 * Inf
.Lmul_inf:
    li      t0, 0x7F80
    or      a0, a2, t0
    ret
.Lmul_ret_a:
    ret
.Lmul_ret_b:
    mv      a0, a1
    ret
.Lmul_qnan:
    li      a0, 0x7FC0
    ret
.Lmul_ret_sign:
    mv      a0, a2
    ret
    .size   bf16_mul_asm, .-bf16_mul_asm

    .globl  bf16_sqrt_asm
    .type   bf16_sqrt_asm, @function
    .align  2
bf16_sqrt_asm:
    # t1 e, t2 fraction
    srli    t1, a0, 7
    andi    t1, t1, 0xFF
    andi    t2, a0, 0x7F
    li      t0, 0xFF
    beq     t1, t0, .Lsqrt_inf_nan

    slli    t3, a0, 17          # zero iff a is +0 or -0
    beqz    t3, .Lsqrt_zero
    srli    t3, a0, 15
    bnez    t3, .Lsqrt_qnan     # negative nonzero
    beqz    t1, .Lsqrt_zero     # positive denormal

    # index = low byte of a
    la      t3, .Lsqrt_tab
    andi    t4, a0, 0xFF
    add     t3, t3, t4
    lbu     t4, 0(t3)

    # exponent (e + 127) >> 1
    addi    t1, t1, 127
    srli    t1, t1, 1
    slli    t1, t1, 7
    or      a0, t1, t4
    ret

.Lsqrt_inf_nan:
    bnez    t2, .Lsqrt_ret      # NaN
    srli    t3, a0, 15
    bnez    t3, .Lsqrt_qnan     # -Inf
.Lsqrt_ret:
    ret                         # +Inf
.Lsqrt_qnan:
    li      a0, 0x7FC0
    ret
.Lsqrt_zero:
    li      a0, 0
    ret
    .size   bf16_sqrt_asm, .-bf16_sqrt_asm

# Square root significands, indexed by the low byte of a: bit 7 is the low
# bit of the biased exponent and bits 0-6 are the stored fraction. Same
# values as sqrt_tab in bf16.c.
    .section .rodata
    .align  2
.Lsqrt_tab:
    .byte   0x35, 0x36, 0x36, 0x37, 0x38, 0x38, 0x39, 0x3a
    .byte   0x3a, 0x3b, 0x3c, 0x3c, 0x3d, 0x3e, 0x3e, 0x3f
    .byte   0x40, 0x40, 0x41, 0x42, 0x42, 0x43, 0x44, 0x44
    .byte   0x45, 0x46, 0x46, 0x47, 0x48, 0x48, 0x49, 0x4a
    .byte   0x4a, 0x4b, 0x4b, 0x4c, 0x4d, 0x4d, 0x4e, 0x4f
    .byte   0x4f, 0x50, 0x50, 0x51, 0x52, 0x52, 0x53, 0x53
    .byte   0x54, 0x55, 0x55, 0x56, 0x56, 0x57, 0x58, 0x58
    .byte   0x59, 0x59, 0x5a, 0x5b, 0x5b, 0x5c, 0x5c, 0x5d
    .byte   0x5d, 0x5e, 0x5f, 0x5f, 0x60, 0x60, 0x61, 0x61
    .byte   0x62, 0x63, 0x63, 0x64, 0x64, 0x65, 0x65, 0x66
    .byte   0x67, 0x67, 0x68, 0x68, 0x69, 0x69, 0x6a, 0x6a
    .byte   0x6b, 0x6b, 0x6c, 0x6d, 0x6d, 0x6e, 0x6e, 0x6f
    .byte   0x6f, 0x70, 0x70, 0x71, 0x71, 0x72, 0x72, 0x73
    .byte   0x73, 0x74, 0x75, 0x75, 0x76, 0x76, 0x77, 0x77
    .byte   0x78, 0x78, 0x79, 0x79, 0x7a, 0x7a, 0x7b, 0x7b
    .byte   0x7c, 0x7c, 0x7d, 0x7d, 0x7e, 0x7e, 0x7f, 0x7f
    .byte   0x00, 0x00, 0x01, 0x01, 0x02, 0x02, 0x03, 0x03
    .byte   0x04, 0x04, 0x05, 0x05, 0x06, 0x06, 0x07, 0x07
    .byte   0x08, 0x08, 0x09, 0x09, 0x0a, 0x0a, 0x0b, 0x0b
    .byte   0x0b, 0x0c, 0x0c, 0x0d, 0x0d, 0x0e, 0x0e, 0x0f
    .byte   0x0f, 0x0f, 0x10, 0x10, 0x11, 0x11, 0x12, 0x12
    .byte   0x13, 0x13, 0x13, 0x14, 0x14, 0x15, 0x15, 0x16
    .byte   0x16, 0x16, 0x17, 0x17, 0x18, 0x18, 0x19, 0x19
    .byte   0x19, 0x1a, 0x1a, 0x1b, 0x1b, 0x1b, 0x1c, 0x1c
    .byte   0x1d, 0x1d, 0x1d, 0x1e, 0x1e, 0x1f, 0x1f, 0x1f
    .byte   0x20, 0x20, 0x21, 0x21, 0x21, 0x22, 0x22, 0x23
    .byte   0x23, 0x23, 0x24, 0x24, 0x25, 0x25, 0x25, 0x26
    .byte   0x26, 0x27, 0x27, 0x27, 0x28, 0x28, 0x28, 0x29
    .byte   0x29, 0x2a, 0x2a, 0x2a, 0x2b, 0x2b, 0x2b, 0x2c
    .byte   0x2c, 0x2d, 0x2d, 0x2d, 0x2e, 0x2e, 0x2e, 0x2f
    .byte   0x2f, 0x2f, 0x30, 0x30, 0x31, 0x31, 0x31, 0x32
    .byte   0x32, 0x32, 0x33, 0x33, 0x33, 0x34, 0x34, 0x35
