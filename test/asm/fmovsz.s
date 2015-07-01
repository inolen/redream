# REGISTER_IN fpscr 0x00140001
# REGISTER_IN dr0   0x3f80000040000000

  .text
  .global start
start:
# FMOV    DRm,DRn SZ=1
  fmov dr0, dr2
# FMOV    @Rm,DRn SZ=1
  mov.l .DATA_ADDR, r0
  fmov @r0, dr4
# FMOV    @Rm,XDn SZ=1
  mov.l .DATA_ADDR, r0
  fmov @r0, xd2
# FMOV    @(R0,Rm),DRn SZ=1
  mov.l .DATA_ADDR, r0
  mov #4, r1
  fmov @(r0, r1), dr6
# FMOV    @(R0,Rm),XDn SZ=1
  mov.l .DATA_ADDR, r0
  mov #8, r1
  fmov @(r0, r1), xd4
# FMOV    @Rm+,DRn SZ=1
  mov.l .DATA_ADDR, r0
  fmov @r0+, dr8
  fmov @r0+, dr10
# FMOV    @Rm+,XDn SZ=1
  mov.l .DATA_ADDR, r0
  fmov @r0+, xd6
  fmov @r0+, xd8
# FMOV    DRm,@Rn SZ=1
  mov.l .DATA_OUT, r0
  fmov dr4, @r0
  mov.l @r0+, r2
  mov.l @r0+, r3
# FMOV    XDm,@Rn SZ=1
  mov.l .DATA_OUT, r0
  fmov xd4, @r0
  mov.l @r0+, r4
  mov.l @r0+, r5
# FMOV    DRm,@-Rn SZ=1
  mov.l .DATA_OUT, r0
  add #8, r0
  fmov dr6, @-r0
  mov.l @r0+, r6
  mov.l @r0+, r7
# FMOV    XDm,@-Rn SZ=1
  mov.l .DATA_OUT, r0
  add #8, r0
  fmov xd6, @-r0
  mov.l @r0+, r8
  mov.l @r0+, r9
# FMOV    DRm,@(R0,Rn) SZ=1
  mov.l .DATA_OUT, r0
  mov #8, r1
  fmov dr0, @(r0, r1)
  add #8, r0
  mov.l @r0+, r10
  mov.l @r0+, r11
# FMOV    XDm,@(R0,Rn) SZ=1
  mov.l .DATA_OUT, r0
  mov #8, r1
  fmov xd2, @(r0, r1)
  add #8, r0
  mov.l @r0+, r12
  mov.l @r0+, r13
  rts 
  nop
  .align 4
.DATA:
  .long 0x40400000
  .long 0x40800000
  .long 0x41100000
  .long 0x41200000
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

# REGISTER_OUT dr2  0x3f80000040000000
# REGISTER_OUT dr4  0x4080000040400000
# REGISTER_OUT xd2  0x4080000040400000
# REGISTER_OUT dr6  0x4110000040800000
# REGISTER_OUT xd4  0x4120000041100000
# REGISTER_OUT dr8  0x4080000040400000
# REGISTER_OUT dr10 0x4120000041100000
# REGISTER_OUT xd6  0x4080000040400000
# REGISTER_OUT xd8  0x4120000041100000
# REGISTER_OUT r2   0x40400000
# REGISTER_OUT r3   0x40800000
# REGISTER_OUT r4   0x41100000
# REGISTER_OUT r5   0x41200000
# REGISTER_OUT r6   0x40800000
# REGISTER_OUT r7   0x41100000
# REGISTER_OUT r8   0x40400000
# REGISTER_OUT r9   0x40800000
# REGISTER_OUT r10  0x40000000
# REGISTER_OUT r11  0x3f800000
# REGISTER_OUT r12  0x40400000
# REGISTER_OUT r13  0x40800000
