test_fmov_load_single:
  # FMOV.S  @Rm,FRn 1111nnnnmmmm1000
  mov.l .DATA_ADDR, r0
  fmov.s @r0, fr2
  rts
  nop
  # REGISTER_OUT fr2 0x10a00000

# TODO it'd be really nice to verify that this is correct for the real hardware
test_fmov_load_double_sz0:
  # FMOV    @Rm,DRn 1111nnn0mmmm1000
  mov.l .DATA_ADDR, r0
  fmov @r0, dr2
  rts
  nop
  # REGISTER_OUT fr2 0x10a00000
  # REGISTER_OUT fr3 0x0

test_fmov_load_double_sz1:
  # REGISTER_IN fpscr 0x00140001
  # FMOV    @Rm,DRn 1111nnn0mmmm1000
  mov.l .DATA_ADDR, r0
  fmov @r0, dr2
  rts
  nop
  # REGISTER_OUT dr2 0x10a0000020e00000

test_fmov_load_double_bank_sz1:
  # REGISTER_IN fpscr 0x00140001
  # FMOV    @Rm,XDn 1111nnn1mmmm1000
  mov.l .DATA_ADDR, r0
  fmov @r0, xd2
  rts
  nop
  # REGISTER_OUT xd2 0x10a0000020e00000

.align 4
.DATA:
  .long 0x10a00000
  .long 0x20e00000

.align 4
.DATA_ADDR:
  .long .DATA
