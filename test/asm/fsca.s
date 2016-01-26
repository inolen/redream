test_fsca:
  # REGISTER_IN r0 16384
  lds r0, fpul
  fsca fpul, dr2
  rts
  nop
  # REGISTER_OUT fr2 0x3f800000
  # REGISTER_OUT fr3 0x80000000
