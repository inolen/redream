# REGISTER_IN fr0 0x40200000
# REGISTER_IN fr1 0x40000000

  .text
  .global start
start:
# FMUL FRm,FRn
  fmul fr0, fr1
  rts 
  nop

# REGISTER_OUT fr1 0x40a00000
