test_fdivd:
  # REGISTER_IN fpscr 0x000c0001
  # REGISTER_IN dr0   0xbfe0000000000000
  # REGISTER_IN dr2   0xc000000000000000
  fdiv dr0, dr2
  rts 
  nop
  # REGISTER_OUT dr2 0x4010000000000000

test_fdivf:
  # REGISTER_IN fr0 0xc0200000
  # REGISTER_IN fr1 0x41200000
  fdiv fr0, fr1
  rts 
  nop
  # REGISTER_OUT fr1 0xc0800000
