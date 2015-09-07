test_addc_nocarry:
  # REGISTER_IN r0 0xfffffffe
  # REGISTER_IN r1 0x1
  addc r1, r0
  movt r1
  rts
  nop
  # REGISTER_OUT r0 0xffffffff
  # REGISTER_OUT r1 0

test_addc_carry_t0:
  # REGISTER_IN r0 0xffffffff
  # REGISTER_IN r1 0x1
  addc r1, r0
  movt r1
  rts
  nop
  # REGISTER_OUT r0 0x0
  # REGISTER_OUT r1 1

test_addc_carry_t1:
  # REGISTER_IN r0 0xffffffff
  # REGISTER_IN r1 0x1
  sett
  addc r1, r0
  movt r1
  rts
  nop
  # REGISTER_OUT r0 0x1
  # REGISTER_OUT r1 1
