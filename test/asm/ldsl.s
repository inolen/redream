  .text
  .global start
start:
# LDS.L   @Rm+,MACH
# STS.L   MACH,@-Rn
  mov.l .OUT, r0
  mov.l .OUT_PLUS8, r1
  mov #0xa, r2
  mov.l r2, @r0
  lds.l @r0+, mach
  sts.l mach, @-r1
  mov.l @r1, r3
  cmp/eq r0, r1
  movt r0
  add r0, r3
# LDS.L   @Rm+,MACL
# STS.L   MACL,@-Rn
  mov.l .OUT, r0
  mov.l .OUT_PLUS8, r1
  mov #0xb, r2
  mov.l r2, @r0
  lds.l @r0+, macl
  sts.l macl, @-r1
  mov.l @r1, r4
  cmp/eq r0, r1
  movt r0
  add r0, r4
# LDS.L   @Rm+,PR
# STS.L   PR,@-Rn
  sts pr, r6
  mov.l .OUT, r0
  mov.l .OUT_PLUS8, r1
  mov #0xc, r2
  mov.l r2, @r0
  lds.l @r0+, pr
  sts.l pr, @-r1
  mov.l @r1, r5
  cmp/eq r0, r1
  movt r0
  add r0, r5
  # restore pr
  lds r6, pr
# LDS.L   @Rm+,FPSCR
# STS.L   FPSCR,@-Rn
  mov.l .OUT, r0
  mov.l .OUT_PLUS8, r1
  mov.l .FSPCR_MASKED_BITS_SET, r2
  mov.l r2, @r0
  lds.l @r0+, fpscr
  sts.l fpscr, @-r1
  mov.l @r1, r6
  cmp/eq r0, r1
  movt r0
  add r0, r6
# LDS.L   @Rm+,FPUL
# STS.L   FPSCR,@-Rn
  mov.l .OUT, r0
  mov.l .OUT_PLUS8, r1
  mov #0xd, r2
  mov.l r2, @r0
  lds.l @r0+, FPUL
  sts.l FPUL, @-r1
  mov.l @r1, r7
  cmp/eq r0, r1
  movt r0
  add r0, r7
  rts 
  nop
  .align 4
.DATA:
  .long 0
  .long 0
  .align 4
.OUT:
  .long .DATA
  .align 4
.OUT_PLUS8:
  .long .DATA+8
  .align 4
.FSPCR_MASKED_BITS_SET:
  .long 0xffd40001

# REGISTER_OUT r3 0xb
# REGISTER_OUT r4 0xc
# REGISTER_OUT r5 0xd
# REGISTER_OUT r6 0x00140002
# REGISTER_OUT r7 0xe
