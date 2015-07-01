# REGISTER_IN fr0 0x40800000

  .text
  .global start
start:
  fsqrt fr0
  rts 
  nop

# REGISTER_OUT fr0 0x40000000
