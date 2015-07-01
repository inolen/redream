  .text
  .global start
start:
# LDC.L   @Rm+,SR
# STC.L   SR,@-Rn
  # write control value before swap
  mov #1, r2
  # swap banks
  mov.l .ALT_SR, r0
  ldc.l @r0+, sr
  # overwrite control value to test swap
  mov #99, r2
  # write sr to mem
  mov.l .OUT_PLUS4, r1
  stc.l sr, @-r1
  mov.l @r1, r3
  # r0 in alt bank should have been post-incremented to 4
  cmp/eq #4, r0
  movt r0
  add r0, r3
  # r1 should been pre-decremented by 4
  mov.l .OUT, r0
  cmp/eq r1, r0
  movt r0
  add r0, r3
  # write back out
  mov.l .OUT, r0
  mov.l r3, @r0
  # swap back again
  mov.l .DEFAULT_SR, r0
  ldc.l @r0+, sr
  # read result from mem
  mov.l .OUT, r0
  mov.l @r0, r3
# LDC.L   @Rm+,Rn_BANK
# STC.L   Rm_BANK,@-Rn
  mov.l .OUT, r0
  mov #1, r1
  mov.l r1, @r0
  mov.l .OUT_PLUS8, r1
  ldc.l @r0+, r4_bank
  mov #99, r4
  stc.l r4_bank, @-r1
  mov.l @r1, r4
  cmp/eq r0, r1
  movt r0
  add r0, r4
# LDC.L   @Rm+,GBR
# STC.L   GBR,@-Rn
  mov.l .OUT, r0
  mov #2, r1
  mov.l r1, @r0
  mov.l .OUT_PLUS8, r1
  ldc.l @r0+, gbr
  stc.l gbr, @-r1
  mov.l @r1, r5
  cmp/eq r0, r1
  movt r0
  add r0, r5
# LDC.L   @Rm+,VBR
# STC.L   VBR,@-Rn
  mov.l .OUT, r0
  mov #3, r1
  mov.l r1, @r0
  mov.l .OUT_PLUS8, r1
  ldc.l @r0+, vbr
  stc.l vbr, @-r1
  mov.l @r1, r6
  cmp/eq r0, r1
  movt r0
  add r0, r6
# LDC.L   @Rm+,SSR
# STC.L   SSR,@-Rn
  mov.l .OUT, r0
  mov #4, r1
  mov.l r1, @r0
  mov.l .OUT_PLUS8, r1
  ldc.l @r0+, ssr
  stc.l ssr, @-r1
  mov.l @r1, r7
  cmp/eq r0, r1
  movt r0
  add r0, r7
# LDC.L   @Rm+,SPC
# STC.L   SPC,@-Rn
  mov.l .OUT, r0
  mov #5, r1
  mov.l r1, @r0
  mov.l .OUT_PLUS8, r1
  ldc.l @r0+, spc
  stc.l spc, @-r1
  mov.l @r1, r8
  cmp/eq r0, r1
  movt r0
  add r0, r8
# STC.L   SGR,@-Rn
# TODO
# LDC.L   @Rm+,DBR
# STC.L   DBR,@-Rn
  mov.l .OUT, r0
  mov #7, r1
  mov.l r1, @r0
  mov.l .OUT_PLUS8, r1
  ldc.l @r0+, dbr
  stc.l dbr, @-r1
  mov.l @r1, r10
  cmp/eq r0, r1
  movt r0
  add r0, r10
  rts 
  nop
  .align 4
.DATA:
  .long 0x500000f0
  .long 0x700000f0
  .long 0x0
  .long 0x0
  .align 4
.OUT:
  .long .DATA+8
  .align 4
.OUT_PLUS4:
  .long .DATA+12
  .align 4
.OUT_PLUS8:
  .long .DATA+16
  .align 4
.ALT_SR:
  .long .DATA
  .align 4
.DEFAULT_SR:
  .long .DATA+4
  .align 4

# REGISTER_OUT r2 1
# REGISTER_OUT r3 0x500000f2
# REGISTER_OUT r4 2
# REGISTER_OUT r5 3
# REGISTER_OUT r6 4
# REGISTER_OUT r7 5
# REGISTER_OUT r8 6
# REGISTER_OUT r10 8
