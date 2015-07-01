# REGISTER_IN r0 7
# REGISTER_IN r1 0

  .little
  .text
  .global start
start:
  # BT      disp
  cmp/eq #7, r0
  bt .L1
  bra .L4
  nop
.L1:
  # BTS     disp
  cmp/eq #7, r0
  bt/s .L3
  add #6, r1
  bra .L4
  nop
.L3:
  add #7, r1
.L4:
  rts
  nop

# REGISTER_OUT r1 13
