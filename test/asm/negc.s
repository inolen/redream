test_negc_zero_nocarry:
  # REGISTER_IN r0 0x700000f0
  # REGISTER_IN r1 0x0
  ldc r0, SR
  negc r1, r2
  stc SR, r0
  rts
  nop
  # REGISTER_OUT r0 0x700000f0
  # REGISTER_OUT r2 0x0

test_negc_zero_carry:
  # REGISTER_IN r0 0x700000f1
  # REGISTER_IN r1 0x0
  ldc r0, SR
  negc r1, r2
  stc SR, r0
  rts
  nop
  # REGISTER_OUT r0 0x700000f1
  # REGISTER_OUT r2 0xffffffff

test_negc_neg_nocarry:
  # REGISTER_IN r0 0x700000f0
  # REGISTER_IN r1 0x80000000
  ldc r0, SR
  negc r1, r2
  stc SR, r0
  rts
  nop
  # REGISTER_OUT r0 0x700000f1
  # REGISTER_OUT r2 0x80000000

test_negc_neg_carry:
  # REGISTER_IN r0 0x700000f1
  # REGISTER_IN r1 0x80000000
  ldc r0, SR
  negc r1, r2
  stc SR, r0
  rts
  nop
  # REGISTER_OUT r0 0x700000f1
  # REGISTER_OUT r2 0x7fffffff

test_negc_pos_nocarry:
  # REGISTER_IN r0 0x700000f0
  # REGISTER_IN r1 0x7fffffff
  ldc r0, SR
  negc r1, r2
  stc SR, r0
  rts
  nop
  # REGISTER_OUT r0 0x700000f1
  # REGISTER_OUT r2 0x80000001

test_negc_pos_carry:
  # REGISTER_IN r0 0x700000f1
  # REGISTER_IN r1 0x7fffffff
  ldc r0, SR
  negc r1, r2
  stc SR, r0
  rts
  nop
  # REGISTER_OUT r0 0x700000f1
  # REGISTER_OUT r2 0x80000000
