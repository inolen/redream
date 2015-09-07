test_fldi0:
  # REGISTER_IN fr0 0x11111111
  fldi0 fr0
  rts
  nop
  # REGISTER_OUT fr0 0x00000000

test_fldi1:
  fldi1 fr0
  rts
  nop
  # REGISTER_OUT fr0 0x3f800000

test_flds_fsts:
  # REGISTER_IN fr0 0x3f800000
  flds fr0, fpul
  fsts fpul, fr1
  rts 
  nop
  # REGISTER_OUT fr1 0x3f800000
