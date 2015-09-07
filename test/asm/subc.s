test_subc_nocarry:
  # REGISTER_IN r0 1
  # REGISTER_IN r1 1
  subc r1, r0
  movt r1
  rts
  nop
  # REGISTER_OUT r0 0x0
  # REGISTER_OUT r1 0

test_subc_carry_t0:
  # REGISTER_IN r0 0
  # REGISTER_IN r1 1
  subc r1, r0
  movt r1
  rts
  nop
  # REGISTER_OUT r0 0xffffffff
  # REGISTER_OUT r1 1

test_subc_carry_t1:
  # REGISTER_IN r0 0
  # REGISTER_IN r1 1
  sett
  subc r1, r0
  movt r1
  rts
  nop
  # REGISTER_OUT r0 0xfffffffe
  # REGISTER_OUT r1 1
