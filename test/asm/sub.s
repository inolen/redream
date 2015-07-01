# REGISTER_IN r1 -24
# REGISTER_IN r2 -11
  .text
  .global start
start:
  sub r1, r2
  rts 
  nop

# REGISTER_OUT r2 13
