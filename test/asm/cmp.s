test_cmpstr:
  # REGISTER_IN r0 0x00000000
  # REGISTER_IN r1 0xffffffff
  # REGISTER_IN r2 0x00f00000
  # REGISTER_IN r3 0x00ff0000
  cmp/str r0, r1
  movt r4
  rts
  nop
  # REGISTER_OUT r4 0
