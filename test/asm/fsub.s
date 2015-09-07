test_fsubd:
  # REGISTER_IN fpscr 0x000c0001
  # REGISTER_IN dr0   0xc01c000000000000
  # REGISTER_IN dr2   0xc010000000000000
  fsub dr0, dr2
  rts 
  nop
  # REGISTER_OUT dr2 0x4008000000000000

test_fsubf:
  # REGISTER_IN fr0 0x40400000
  # REGISTER_IN fr1 0xbf800000
  fsub fr0, fr1
  rts 
  nop
  # REGISTER_OUT fr1 0xc0800000

