test_bf:
  # REGISTER_IN r0 8
  # REGISTER_IN r1 0
  cmp/eq #7, r0
  bf .L1
  rts
  nop
.L1:
  mov #3, r1
  rts
  nop
  # REGISTER_OUT r1 3

test_bfs:
  # REGISTER_IN r0 8
  # REGISTER_IN r1 0
  cmp/eq #7, r0
  bf/s .L2
  add #6, r1
  rts
  nop
.L2:
  add #7, r1
  rts
  nop
  # REGISTER_OUT r1 13
