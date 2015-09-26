test_div0u:
  # REGISTER_IN r0 0x700000f0
  ldc r0, sr
  div0u
  movt r1
  rts
  nop
  # REGISTER_OUT r1 0x0

test_div0s_ndividend_ndivisor:
  # REGISTER_IN r0 0x700000f0
  # REGISTER_IN r1 0x2
  # REGISTER_IN r2 0x4
  ldc r0, sr
  div0s r1, r2
  movt r3
  rts
  nop
  # REGISTER_OUT r3 0x0

test_div0s_pdividend_pdivisor:
  # REGISTER_IN r0 0x700000f0
  # REGISTER_IN r1 0xfffffffe
  # REGISTER_IN r2 0xfffffffc
  ldc r0, sr
  div0s r1, r2
  movt r3
  rts
  nop
  # REGISTER_OUT r3 0x0

test_div0s_ndividend_pdivisor:
  # REGISTER_IN r0 0x700000f0
  # REGISTER_IN r1 0x2
  # REGISTER_IN r2 0xfffffffc
  ldc r0, sr
  div0s r1, r2
  movt r3
  rts
  nop
  # REGISTER_OUT r3 0x1

test_div0s_pdividend_ndivisor:
  # REGISTER_IN r0 0x700000f0
  # REGISTER_IN r1 0xfffffffe
  # REGISTER_IN r2 0x4
  ldc r0, sr
  div0s r1, r2
  movt r3
  rts
  nop
  # REGISTER_OUT r3 0x1
