# REGISTER_IN fr0 0x11111111

  .text
  .global start
start:
# FLDI0  FRn
  fldi0 fr0
# FLDI1  FRn
  fldi1 fr1
# FLDS FRm,FPUL
  flds fr1, fpul
# FSTS FPUL,FRn
  fsts fpul, fr2
  rts 
  nop

# REGISTER_OUT fr0 0x00000000
# REGISTER_OUT fr1 0x3f800000
# REGISTER_OUT fr2 0x3f800000
