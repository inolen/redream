test_fmov_restore_single:
  # FMOV.S  @Rm+,FRn 1111nnnnmmmm1001
  mov.l .DATA_ADDR, r0
  mov r0, r1
  fmov.s @r1+, fr2
  sub r0, r1
  rts
  nop
  # REGISTER_OUT fr2 0x10a00000
  # REGISTER_OUT r1 0x4

# TODO it'd be really nice to verify that this is correct for the real hardware
test_fmov_restore_double_sz0:
  # FMOV    @Rm+,DRn 1111nnn0mmmm1001
  mov.l .DATA_ADDR, r0
  mov r0, r1
  fmov @r1+, dr2
  sub r0, r1
  rts
  nop
  # REGISTER_OUT fr2 0x10a00000
  # REGISTER_OUT fr3 0x0
  # REGISTER_OUT r1 0x4

test_fmov_restore_double_sz1:
  # REGISTER_IN fpscr 0x00140001
  # FMOV    @Rm+,DRn 1111nnn0mmmm1001
  mov.l .DATA_ADDR, r0
  mov r0, r1
  fmov @r1+, dr2
  sub r0, r1
  rts
  nop
  # REGISTER_OUT dr2 0x10a0000020e00000
  # REGISTER_OUT r1 0x8

test_fmov_restore_double_bank_sz1:
  # REGISTER_IN fpscr 0x00140001
  # FMOV    @Rm+,XDn 1111nnn1mmmm1001
  mov.l .DATA_ADDR, r0
  mov r0, r1
  fmov @r1+, xd2
  sub r0, r1
  rts
  nop
  # REGISTER_OUT xd2 0x10a0000020e00000
  # REGISTER_OUT r1 0x8

.align 4
.DATA:
  .long 0x10a00000
  .long 0x20e00000

.align 4
.DATA_ADDR:
  .long .DATA
