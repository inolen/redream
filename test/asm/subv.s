# truth table for signed subtraction, 0 for positive, 1 for negative
# ------------------------------------------------------------------
# 0 - 0 = 0
# 0 - 0 = 1
# 0 - 1 = 0
# 0 - 1 = 1 *OVERFLOW*
# 1 - 0 = 0 *OVERFLOW*
# 1 - 0 = 1
# 1 - 1 = 0
# 1 - 1 = 1

test_subv_ppp:
  # REGISTER_IN r0 0x7fffffff
  # REGISTER_IN r1 0x7ffffffe
  subv r1, r0
  movt r1
  rts
  nop
  # REGISTER_OUT r0 0x00000001
  # REGISTER_OUT r1 0

test_subv_ppn:
  # REGISTER_IN r0 0x7ffffffe
  # REGISTER_IN r1 0x7fffffff
  subv r1, r0
  movt r1
  rts
  nop
  # REGISTER_OUT r0 0xffffffff
  # REGISTER_OUT r1 0

test_subv_pnp:
  # REGISTER_IN r0 0x7ffffffe
  # REGISTER_IN r1 0xffffffff
  subv r1, r0
  movt r1
  rts
  nop
  # REGISTER_OUT r0 0x7fffffff
  # REGISTER_OUT r1 0

test_subv_pnn_overflow:
  # REGISTER_IN r0 0x7fffffff
  # REGISTER_IN r1 0xffffffff
  subv r1, r0
  movt r1
  rts
  nop
  # REGISTER_OUT r0 0x80000000
  # REGISTER_OUT r1 1

test_subv_npp_overflow:
  # REGISTER_IN r0 0x80000000
  # REGISTER_IN r1 0x00000001
  subv r1, r0
  movt r1
  rts
  nop
  # REGISTER_OUT r0 0x7fffffff
  # REGISTER_OUT r1 1

test_subv_npn:
  # REGISTER_IN r0 0x80000001
  # REGISTER_IN r1 0x00000001
  subv r1, r0
  movt r1
  rts
  nop
  # REGISTER_OUT r0 0x80000000
  # REGISTER_OUT r1 0

test_subv_nnp:
  # REGISTER_IN r0 0x80000001
  # REGISTER_IN r1 0x80000000
  subv r1, r0
  movt r1
  rts
  nop
  # REGISTER_OUT r0 0x00000001
  # REGISTER_OUT r1 0

test_subv_nnn:
  # REGISTER_IN r0 0x80000000
  # REGISTER_IN r1 0x80000001
  subv r1, r0
  movt r1
  rts
  nop
  # REGISTER_OUT r0 0xffffffff
  # REGISTER_OUT r1 0
