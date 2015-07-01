# REGISTER_IN fr0 0x40400000
# REGISTER_IN fr1 0xbf800000

  .text
  .global start
start:
# FADD FRm,FRn PR=0 1111nnnnmmmm0000
  fadd fr0, fr1
  rts 
  nop

# REGISTER_OUT fr1 0x40000000
