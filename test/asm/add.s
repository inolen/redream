# REGISTER_IN r0 -4
# REGISTER_IN r1 17

  .text
  .global start
start:
  add r0, r1
  rts 
  nop

# REGISTER_OUT r1 13
