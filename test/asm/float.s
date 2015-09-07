test_floatd:
  # REGISTER_IN fpscr 0x000c0001
  # REGISTER_IN r1    0x00000004
  lds r1, fpul
  float fpul, dr2
  rts
  nop
  # REGISTER_OUT dr2 0x4010000000000000

test_floatf:
  # REGISTER_IN r0 0x00000002
  lds r0, fpul
  float fpul, fr3
  rts 
  nop
  # REGISTER_OUT fr3 0x40000000

