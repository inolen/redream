# REGISTER_IN fpscr 0x000c0001
# REGISTER_IN xd0   0x4020000000000000
# REGISTER_IN xd2   0x4024000000000000

  .text
  .global start
start:
# FMOV    XDm,DRn PR=1
  fmov xd0, dr0
# FMOV    DRm,XDn PR=1
  fmov dr0, xd4
# FMOV    XDm,XDn PR=1
  fmov xd2, xd6
# FMOV    @Rm,XDn PR=1
  mov.l .DATA_ADDR, r0
  fmov @r0, xd8
# FMOV    @Rm,DRn PR=1
  mov.l .DATA_ADDR, r0
  fmov @r0, dr2
# FMOV    @(R0,Rm),XDn PR=1
  mov.l .DATA_ADDR, r0
  mov #8, r1
  fmov @(r0, r1), xd10
# FMOV    @Rm+,XDn PR=1
  mov.l .DATA_ADDR, r0
  fmov @r0+, xd12
  fmov @r0+, xd14
# FMOV    XDm,@Rn PR=1
  mov.l .DATA_OUT, r0
  fmov xd0, @r0
  mov.l @r0+, r2
  mov.l @r0+, r3
# FMOV    XDm,@-Rn PR=1
  mov.l .DATA_OUT, r0
  add #8, r0
  fmov xd2, @-r0
  mov.l @r0+, r4
  mov.l @r0+, r5
# FMOV    XDm,@(R0,Rn) PR=1
  mov.l .DATA_OUT, r0
  mov #8, r1
  fmov xd0, @(r0, r1)
  add #8, r0
  mov.l @r0+, r6
  mov.l @r0+, r7
  rts 
  nop
  .align 4
.DATA:
  .long 0x40280000
  .long 0x00000000
  .long 0x402e0000
  .long 0x00000000
  .long 0x0
  .long 0x0
  .long 0x0
  .long 0x0
  .align 4
.DATA_ADDR:
  .long .DATA
  .align 4
.DATA_OUT:
  .long .DATA+16

# REGISTER_OUT dr0  0x4020000000000000
# REGISTER_OUT xd4  0x4020000000000000
# REGISTER_OUT xd6  0x4024000000000000
# REGISTER_OUT xd8  0x4028000000000000
# REGISTER_OUT dr2  0x4028000000000000
# REGISTER_OUT xd10 0x402e000000000000
# REGISTER_OUT xd12 0x4028000000000000
# REGISTER_OUT xd14 0x402e000000000000
# REGISTER_OUT r2   0x40200000
# REGISTER_OUT r3   0x00000000
# REGISTER_OUT r4   0x40240000
# REGISTER_OUT r5   0x00000000
# REGISTER_OUT r6   0x40200000
# REGISTER_OUT r7   0x00000000
