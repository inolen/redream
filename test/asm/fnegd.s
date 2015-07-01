# REGISTER_IN fpscr 0x000c0001
# REGISTER_IN dr0   0x4014000000000000

  .text
  .global start
start:
# FNEG DRn
  fneg dr0
  rts 
  nop

# REGISTER_OUT dr0 0xc014000000000000
