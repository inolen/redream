test_mull:
  # REGISTER_IN r0 0x00fffffe
  # REGISTER_IN r1 0x00000004
  mul.l r1, r0
  sts macl, r0
  rts
  nop
  # REGISTER_OUT r0 0x03fffff8

test_muls:
  # REGISTER_IN r0 0x00fffffe
  # REGISTER_IN r1 0x00000004
  muls r1, r0
  sts macl, r0
  rts
  nop
  # REGISTER_OUT r0 0xfffffff8

test_mulu:
  # REGISTER_IN r0 0x00fffffe
  # REGISTER_IN r1 0x00000004
  mulu r1, r0
  sts macl, r0
  rts
  nop
  # REGISTER_OUT r0 0x0003fff8
