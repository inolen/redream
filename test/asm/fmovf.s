# REGISTER_IN fr0 0x40400000

  .text
  .global start
start:
# FMOV    FRm,FRn
  fmov fr0, fr1
# FMOV.S  @Rm,FRn
  mov.l .DATA_ADDR, r0
  fmov.s @r0, fr2
# FMOV.S  @(R0,Rm),FRn
  mov.l .DATA_ADDR, r0
  mov #4, r1
  fmov.s @(r0, r1), fr3
# FMOV.S  @Rm+,FRn
  mov.l .DATA_ADDR, r0
  fmov.s @r0+, fr4
  fmov.s @r0+, fr5
# FMOV.S  FRm,@Rn
  mov.l .DATA_OUT, r0
  fmov.s fr0, @r0
  mov.l @r0, r2
# FMOV.S  FRm,@-Rn
  mov.l .DATA_OUT, r0
  add #8, r0
  fmov.s fr2, @-r0
  mov.l @r0, r3
# FMOV.S  FRm,@(R0,Rn)
  mov.l .DATA_OUT, r0
  mov #4, r1
  fmov.s fr0, @(r0, r1)
  mov.l @(r0, r1), r4
  rts 
  nop
  .align 4
.DATA:
  .long 0x40a00000
  .long 0x40e00000
  .long 0x0
  .long 0x0
  .align 4
.DATA_ADDR:
  .long .DATA
  .align 4
.DATA_OUT:
  .long .DATA+8

# REGISTER_OUT fr1 0x40400000
# REGISTER_OUT fr2 0x40a00000
# REGISTER_OUT fr3 0x40e00000
# REGISTER_OUT fr4 0x40a00000
# REGISTER_OUT fr5 0x40e00000
# REGISTER_OUT r2  0x40400000
# REGISTER_OUT r3  0x40a00000
# REGISTER_OUT r4  0x40400000
