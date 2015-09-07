# truth table for signed additition, 0 for positive, 1 for negative
# ------------------------------------------------------------------
# 0 + 0 = 0
# 0 + 0 = 1 *OVERFLOW*
# 0 + 1 = 0
# 0 + 1 = 1
# 1 + 0 = 0
# 1 + 0 = 1
# 1 + 1 = 0 *OVERFLOW*
# 1 + 1 + 1

test_addv_ppp:
  # REGISTER_IN r0 0x1
  # REGISTER_IN r1 0x7ffffffe
  addv r0, r1
  movt r0
  rts
  nop
  # REGISTER_OUT r1 0x7fffffff
  # REGISTER_OUT r0 0

test_addv_ppn_overflow:
  # REGISTER_IN r0 0x1
  # REGISTER_IN r1 0x7fffffff
  addv r0, r1
  movt r0
  rts
  nop
  # REGISTER_OUT r1 0x80000000
  # REGISTER_OUT r0 1

test_addv_pnp:
  # REGISTER_IN r0 0x80000001
  # REGISTER_IN r1 0x7fffffff
  addv r0, r1
  movt r0
  rts
  nop
  # REGISTER_OUT r1 0x00000000
  # REGISTER_OUT r0 0

test_addv_pnn:
  # REGISTER_IN r0 0x80000000
  # REGISTER_IN r1 0x1
  addv r0, r1
  movt r0
  rts
  nop
  # REGISTER_OUT r1 0x80000001
  # REGISTER_OUT r0 0

test_addv_npp:
  # REGISTER_IN r0 0x7fffffff
  # REGISTER_IN r1 0x80000001
  addv r0, r1
  movt r0
  rts
  nop
  # REGISTER_OUT r1 0x00000000
  # REGISTER_OUT r0 0

test_addv_npn:
  # REGISTER_IN r0 0x1
  # REGISTER_IN r1 0x80000000
  addv r0, r1
  movt r0
  rts
  nop
  # REGISTER_OUT r1 0x80000001
  # REGISTER_OUT r0 0

test_addv_nnp_overflow:
  # REGISTER_IN r0 0xffffffff
  # REGISTER_IN r1 0x80000000
  addv r0, r1
  movt r0
  rts
  nop
  # REGISTER_OUT r1 0x7fffffff
  # REGISTER_OUT r0 1

test_addv_nnn:
  # REGISTER_IN r0 0xffffffff
  # REGISTER_IN r1 0x80000001
  addv r0, r1
  movt r0
  rts
  nop
  # REGISTER_OUT r1 0x80000000
  # REGISTER_OUT r0 0
