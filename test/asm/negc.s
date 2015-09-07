test_negc_nocarry:
  # REGISTER_IN r0 0x700000f0
  # REGISTER_IN r1 -4
  ldc r0, SR
  negc r1, r2
  rts
  nop
  # REGISTER_OUT r2 4

test_negc_carry:
  # REGISTER_IN r0 0x700000f1
  # REGISTER_IN r1 -4
  ldc r0, SR
  negc r1, r2
  rts
  nop
  # REGISTER_OUT r2 3
