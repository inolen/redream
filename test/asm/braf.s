# REGISTER_IN r0 2
# REGISTER_IN r1 4

  .little
  .text
  .global start
start:
# BRAF      Rn
  braf r0
  nop
  add #7, r1
  add #9, r1
  rts
  nop

# REGISTER_OUT r1 13
