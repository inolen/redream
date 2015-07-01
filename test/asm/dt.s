# REGISTER_IN r0 13
# REGISTER_IN r1 0

  .text
  .global start
start:
  add #1, r1
  dt r0
  bf start
  rts 
  nop

# REGISTER_OUT r0 0
# REGISTER_OUT r1 13
