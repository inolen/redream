test_movt:
  # REGISTER_IN r0 3
  cmp/eq #3, r0
  movt r1
  cmp/eq #5, r0
  movt r2
  rts
  nop
  # REGISTER_OUT r1 1
  # REGISTER_OUT r2 0
