test_fmov_single:
  # REGISTER_IN fr2 0x40400000
  # FMOV    FRm,FRn 1111nnnnmmmm1100
  fmov fr2, fr3
  rts 
  nop
  # REGISTER_OUT fr3 0x40400000

# TODO it'd be really nice to verify that this is correct for the real hardware
test_fmov_double_sz0:
  # REGISTER_IN dr2 0x4040000030300000
  # FMOV    DRm,DRn 1111nnn0mmm01100
  fmov dr2, dr4
  rts 
  nop
  # REGISTER_OUT fr4 0x40400000
  # REGISTER_OUT fr5 0x0

test_fmov_double_sz1:
  # REGISTER_IN fpscr 0x00140001
  # REGISTER_IN dr2 0x4040000030300000
  # FMOV    DRm,DRn 1111nnn0mmm01100
  fmov dr2, dr4
  rts 
  nop
  # REGISTER_OUT dr4 0x4040000030300000

test_fmov_double_bank_sz1:
  # REGISTER_IN fpscr 0x00140001
  # REGISTER_IN xd0 0x4040000030300000
  # REGISTER_IN xd2 0x80800000a0a00000
  # FMOV    XDm,DRn 1111nnn0mmm11100
  fmov xd0, dr4
  # FMOV    DRm,XDn 1111nnn1mmm01100
  fmov dr4, xd6
  # FMOV    XDm,XDn 1111nnn1mmm11100
  fmov xd2, xd8
  rts
  nop
  # REGISTER_OUT dr4 0x4040000030300000
  # REGISTER_OUT xd6 0x4040000030300000
  # REGISTER_OUT xd8 0x80800000a0a00000
