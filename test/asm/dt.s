test_dt:
  # REGISTER_IN r0 13
  # REGISTER_IN r1 0
  add #1, r1
  dt r0
  bf test_dt
  rts 
  nop
