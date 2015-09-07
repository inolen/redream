test_fmuld:
  # REGISTER_IN fpscr 0x000c0001
  # REGISTER_IN dr0   0x4008000000000000
  # REGISTER_IN dr2   0xc01c000000000000
  fmul dr0, dr2
  rts 
  nop
  # REGISTER_OUT dr2 0xc035000000000000

test_fmulf:
  # REGISTER_IN fr0 0x40200000
  # REGISTER_IN fr1 0x40000000
  fmul fr0, fr1
  rts 
  nop
  # REGISTER_OUT fr1 0x40a00000

