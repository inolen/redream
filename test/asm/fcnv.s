test_fcnvds:
  # REGISTER_IN fpscr 0x000c0001
  # REGISTER_IN dr6 0x3ff0000000000000
  fcnvds dr6, fpul
  sts fpul, r3
  # REGISTER_OUT r3 0x3f800000

test_fcnvsd:
  # REGISTER_IN fpscr 0x000c0001
  # REGISTER_IN r1 0x3f800000
  lds r1, fpul
  fcnvsd fpul, dr4
  rts
  nop
  # REGISTER_OUT dr4 0x3ff0000000000000
