test_fcmpeqd:
  # REGISTER_IN fpscr 0x000c0001
  # REGISTER_IN dr0   0x4014000000000000
  # REGISTER_IN dr2   0xc018000000000000
  # REGISTER_IN dr4   0xc018000000000000
  fcmp/eq dr0, dr2
  movt r0
  fcmp/eq dr2, dr4
  movt r1
  rts 
  nop
  # REGISTER_OUT r0 0
  # REGISTER_OUT r1 1

test_fcmpeqf:
  # REGISTER_IN fr0 0x40400000
  # REGISTER_IN fr1 0xbf800000
  # REGISTER_IN fr2 0xbf800000
  fcmp/eq fr0, fr1
  movt r0
  fcmp/eq fr1, fr2
  movt r1
  rts 
  nop
  # REGISTER_OUT r0 0
  # REGISTER_OUT r1 1

