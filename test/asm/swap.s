test_swapb:
  # REGISTER_IN r0 0xfffffff0
  swap.b r0, r1
  rts
  nop
  # REGISTER_OUT r1 0xfffff0ff

test_swapw:
  # REGISTER_IN r0 0xfffffff0
  swap.w r0, r1
  rts
  nop
  # REGISTER_OUT r1 0xfff0ffff

test_xtrct:
  # REGISTER_IN r0 0xfffff0ff
  # REGISTER_IN r1 0xfff0ffff
  xtrct r0, r1
  rts
  nop
  # REGISTER_OUT r1 0xf0fffff0
