test_fsqrtd:
  # REGISTER_IN fpscr 0x000c0001
  # REGISTER_IN dr0   0x4010000000000000
  fsqrt dr0
  rts 
  nop
  # REGISTER_OUT dr0 0x4000000000000000

test_fsqrtf:
  # REGISTER_IN fr0 0x40800000
  fsqrt fr0
  rts 
  nop
  # REGISTER_OUT fr0 0x40000000

