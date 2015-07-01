# REGISTER_IN r0 7
# REGISTER_IN r1 0

  .little
  .text
  .global start
start:
# BF      disp
  cmp/eq #8, r0
  bf .L1
  bra .L4
  nop
.L1:
# BFS     disp
  cmp/eq #9, r0
  bf/s .L3
  add #6, r1
  bra .L4
  nop
.L3:
  add #7, r1
.L4:
  rts
  nop

# REGISTER_OUT r1 13
