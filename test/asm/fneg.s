test_fnegd:
  # REGISTER_IN fpscr 0x000c0001
  # REGISTER_IN dr0   0x4014000000000000
  fneg dr0
  rts 
  nop
  # REGISTER_OUT dr0 0xc014000000000000

test_fnegf:
  # REGISTER_IN fr0 0x40800000
  fneg fr0
  rts 
  nop
  # REGISTER_OUT fr0 0xc0800000

