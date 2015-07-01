# REGISTER_IN fr0 0xc0000000
# REGISTER_IN fr1 0xc0a00000
# REGISTER_IN fr2 0x40400000

  .text
  .global start
start:
# FMAC FR0,FRm,FRn
  fmac fr0, fr1, fr2
  rts 
  nop

# REGISTER_OUT fr2 0x41500000
