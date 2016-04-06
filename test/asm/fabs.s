test_fabsd:
  # REGISTER_IN fpscr 0x000c0001
  # REGISTER_IN dr2   0xc0100000f0000000
  fabs dr2
  rts 
  nop
  # REGISTER_OUT dr2 0x40100000f0000000

test_fabsf:
  # REGISTER_IN fr1 0xc0800000
  fabs fr1
  rts 
  nop
  # REGISTER_OUT fr1 0x40800000

