# REGISTER_IN fpscr 0x000c0001
# REGISTER_IN dr2   0xc010000000000000

  .text
  .global start
start:
# FABS DRn
  fabs dr2
  rts 
  nop

# REGISTER_OUT dr2 0x4010000000000000
