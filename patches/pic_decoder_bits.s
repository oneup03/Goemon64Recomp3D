.include "macro.inc"

.set noat
.set noreorder

.section .recomp_patch, "ax"

# @recomp The "PIC" image decoder's bit reader (wave_uty.c), the original's
# instructions after a count of the reads: every DECODE_YIELD_CALLS-th read
# goes to read_bits_after_yield (pic_decoder.c), which lets the higher-priority
# threads run first. In assembly so that the usual path stays a leaf: clang
# saves the return address on entry to the C version, as it cannot make the
# jump to the yield a tail call.
glabel func_80014CC4_158C4
    lui        $t0, %hi(decode_yield_calls)
    lw         $t1, %lo(decode_yield_calls)($t0)
    addiu      $t1, $t1, 0x1
    sltiu      $at, $t1, 2048                       # DECODE_YIELD_CALLS
    beqz       $at, .Lyield
    sw         $t1, %lo(decode_yield_calls)($t0)
    # The original, from here.
    lui        $a2, %hi(D_8006ADC4_6B9C4)
    addiu      $a2, $a2, %lo(D_8006ADC4_6B9C4)
    lw         $a1, 0x0($a2)
    or         $v1, $zero, $zero
    addiu      $t1, $zero, 0x8
    slt        $at, $a1, $a0
    beqz       $at, .L80014D34_15934
    lui        $t0, %hi(D_80168160_168D60)
    lui        $a3, %hi(D_8006ADCC_6B9CC)
    addiu      $a3, $a3, %lo(D_8006ADCC_6B9CC)
    addiu      $t0, $t0, %lo(D_80168160_168D60)
.L80014CF0_158F0:
    lw         $t6, 0x0($a3)
    lw         $v0, 0x0($t0)
    addiu      $t7, $zero, 0x1
    sllv       $t8, $t7, $a1
    subu       $a0, $a0, $a1
    addiu      $t9, $t8, -0x1
    lbu        $t4, 0x0($v0)
    and        $t2, $t6, $t9
    sllv       $t3, $t2, $a0
    slt        $at, $t1, $a0
    addiu      $t5, $v0, 0x1
    or         $v1, $v1, $t3
    sw         $t5, 0x0($t0)
    sw         $t1, 0x0($a2)
    or         $a1, $t1, $zero
    bnez       $at, .L80014CF0_158F0
    sw         $t4, 0x0($a3)
.L80014D34_15934:
    lui        $a3, %hi(D_8006ADCC_6B9CC)
    addiu      $a3, $a3, %lo(D_8006ADCC_6B9CC)
    lw         $t8, 0x0($a3)
    addiu      $t2, $zero, 0x1
    subu       $t7, $a1, $a0
    sllv       $t3, $t2, $a0
    addiu      $t4, $t3, -0x1
    srlv       $t9, $t8, $t7
    and        $t5, $t9, $t4
    sw         $t7, 0x0($a2)
    or         $v0, $t5, $v1
    jr         $ra
    nop
.Lyield:
    j          read_bits_after_yield
    nop
endlabel func_80014CC4_158C4
