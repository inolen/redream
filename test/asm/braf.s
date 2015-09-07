test_braf:
  # REGISTER_IN r0 2
  # REGISTER_IN r1 4
  braf r0
  nop
  add #7, r1
  add #9, r1
  rts
  nop
  # REGISTER_OUT r1 13
