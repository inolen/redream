# REGISTER_IN fpscr 0x000c0001
# REGISTER_IN dr0   0x4014000000000000
# REGISTER_IN dr2   0xc018000000000000

  .text
  .global start
start:
# FADD DRm,DRn PR=1 1111nnn0mmm00000
  fadd dr0, dr2
  rts 
  nop

# REGISTER_OUT dr2 0xbff0000000000000
