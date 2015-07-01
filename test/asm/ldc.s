  .text
  .global start
start:
# LDC     Rm,SR
# STC     SR,Rn
  # write control value before swap
  mov #1, r2
  # swap banks
  mov.l .ALT_SR, r0
  ldc r0, sr
  # overwrite control value to test swap
  mov #99, r2
  # write sr to mem
  stc sr, r2
  mov.l .DATA_ADDR, r0
  mov.l r2, @r0
  # swap back again
  mov.l .DEFAULT_SR, r0
  ldc r0, sr
  # read sr from mem
  mov.l .DATA_ADDR, r0
  mov.l @r0, r3
# LDCRBANK   Rm,Rn_BANK
# STCRBANK   Rm_BANK,Rn
  mov #1, r0
  ldc r0, r4_bank
  mov #99, r4
  stc r4_bank, r4
# LDC     Rm,GBR
# STC     GBR,Rn
  mov #2, r0
  ldc r0, gbr
  stc gbr, r5
# LDC     Rm,VBR
# STC     VBR,Rn
  mov #3, r0
  ldc r0, vbr
  stc vbr, r6
# LDC     Rm,SSR
# STC     SSR,Rn
  mov #4, r0
  ldc r0, ssr
  stc ssr, r7
# LDC     Rm,SPC
# STC     SPC,Rn
  mov #5, r0
  ldc r0, spc
  stc spc, r8
# STC     SGR,Rn
# TODO
#  mov r0, r15
#  trapa
#  stc sgr, r9
# LDC     Rm,DBR
# STC     DBR,Rn
  mov #7, r0
  ldc r0, dbr
  stc dbr, r10
  rts 
  nop
  .align 4
.DATA:
  .long 0x0
  .align 4
.DATA_ADDR:
  .long .DATA
  .align 4
.ALT_SR:
  .long 0x500000f0
  .align 4
.DEFAULT_SR:
  .long 0x700000f0

# REGISTER_OUT r2 1
# REGISTER_OUT r3 0x500000f0
# REGISTER_OUT r4 1
# REGISTER_OUT r5 2
# REGISTER_OUT r6 3
# REGISTER_OUT r7 4
# REGISTER_OUT r8 5
# REGISTER_OUT r10 7
