# REGISTER_IN fpscr 0x000c0001
# REGISTER_IN dr0   0xbfe0000000000000
# REGISTER_IN dr2   0xc000000000000000

  .text
  .global start
start:
# FDIV DRm,DRn
  fdiv dr0, dr2
  rts 
  nop

# REGISTER_OUT dr2 0x4010000000000000
