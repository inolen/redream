# REGISTER_IN fr0 0x40800000

  .text
  .global start
start:
  fsrra fr0
  rts 
  nop

# REGISTER_OUT fr0 0x3f000000
