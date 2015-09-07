test_bt:
  # REGISTER_IN r0 7
  # REGISTER_IN r1 0
  cmp/eq #7, r0
  bt .L1
  rts
  nop
.L1:
  mov #3, r1
  rts
  nop
  # REGISTER_OUT r1 3

test_bts:
  # REGISTER_IN r0 7
  # REGISTER_IN r1 0
  # BTS     disp
  cmp/eq #7, r0
  bt/s .L2
  add #6, r1
  rts
  nop
.L2:
  add #7, r1
  rts
  nop
  # REGISTER_OUT r1 13
