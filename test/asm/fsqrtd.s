# REGISTER_IN fpscr 0x000c0001
# REGISTER_IN dr0   0x4010000000000000

  .text
  .global start
start:
  fsqrt dr0
  rts 
  nop

# REGISTER_OUT dr0 0x4000000000000000
