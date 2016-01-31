# FMOV    XDm,@(R0,Rn) 1111nnnnmmm10111

test_fmov_index_store_single:
  # REGISTER_IN fr0 0x10a00000
  # FMOV.S  FRm,@(R0,Rn) 1111nnnnmmmm0111
  mov.l .DATA_OUT, r0
  mov #4, r1
  fmov.s fr0, @(r0,r1)
  mov.l @(r0,r1), r2
  rts
  nop
  # REGISTER_OUT r2 0x10a00000

# TODO it'd be really nice to verify that this is correct for the real hardware
test_fmov_index_store_double_sz0:
  # REGISTER_IN dr0 0x10a0000020e00000
  # FMOV    DRm,@(R0,Rn) 1111nnnnmmm00111
  mov.l .DATA_OUT, r0
  mov #4, r1
  fmov dr0, @(r0,r1)
  mov.l @(r0,r1), r2
  add #4, r1
  mov.l @(r0,r1), r3
  rts
  nop
  # REGISTER_OUT r2 0x10a00000
  # REGISTER_OUT r3 0x0

test_fmov_index_store_double_sz1:
  # REGISTER_IN fpscr 0x00140001
  # REGISTER_IN dr0 0x20a0000030e00000
  # FMOV    DRm,@Rn 1111nnnnmmm01010
  mov.l .DATA_OUT, r0
  mov #4, r1
  fmov dr0, @(r0,r1)
  mov.l @(r0,r1), r2
  add #4, r1
  mov.l @(r0,r1), r3
  rts
  nop
  # REGISTER_OUT r2 0x20a00000
  # REGISTER_OUT r3 0x30e00000

test_fmov_index_store_double_bank_sz1:
  # REGISTER_IN fpscr 0x00140001
  # REGISTER_IN xd0 0x30a0000040e00000
  # FMOV    XDm,@Rn 1111nnnnmmm11010
  mov.l .DATA_OUT, r0
  mov #4, r1
  fmov xd0, @(r0,r1)
  mov.l @(r0,r1), r2
  add #4, r1
  mov.l @(r0,r1), r3
  rts
  nop
  # REGISTER_OUT r2 0x30a00000
  # REGISTER_OUT r3 0x40e00000

.align 4
.DATA:
  .long 0x0
  .long 0x0
  .long 0x0

.align 4
.DATA_OUT:
  .long .DATA
