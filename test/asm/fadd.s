test_faddd:
  # REGISTER_IN fpscr 0x000c0001
  # REGISTER_IN dr0   0x4014000000000000
  # REGISTER_IN dr2   0xc018000000000000
  fadd dr0, dr2
  rts 
  nop
  # REGISTER_OUT dr2 0xbff0000000000000

test_faddf:
  # REGISTER_IN fr0 0x40400000
  # REGISTER_IN fr1 0xbf800000
  fadd fr0, fr1
  rts 
  nop
  # REGISTER_OUT fr1 0x40000000

