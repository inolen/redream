test_xor:
  # REGISTER_IN r0 0x00ffffff
  # REGISTER_IN r1 0xffffff00
  xor r1, r0
  # REGISTER_OUT r0 0xff0000ff
  rts
  nop

test_xor_imm:
  # REGISTER_IN r0 0x00ffffff
  xor #0xff, r0
  rts 
  nop
  # REGISTER_OUT r0 0x00ffff00
