# REGISTER_IN r0 4

  .little
  .text
  .global start
start:
# BRA      label
  bra .L2
  nop
.L1:
  add #1, r0
.L2:
  add #9, r0
.L4:
  rts
  nop

# REGISTER_OUT r0 13
