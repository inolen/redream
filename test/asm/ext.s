test_extsb:
  # REGISTER_IN r0 0xff
  exts.b r0, r1
  rts
  nop
  # REGISTER_OUT r1 0xffffffff

test_extsw:
  # REGISTER_IN r0 0xffff
  exts.w r0, r1
  rts
  nop
  # REGISTER_OUT r1 0xffffffff

test_extub:
  # REGISTER_IN r0 0xfffa3002
  extu.b r0, r1
  rts
  nop
  # REGISTER_OUT r1 0x2

test_extuw:
  # REGISTER_IN r0 0xfffa3002
  extu.w r0, r1
  rts
  nop
  # REGISTER_OUT r1 0x3002
