# REGISTER_IN fpscr 0x000c0001
# REGISTER_IN r1    0x00000004

  .text
  .global start
start:
# FLOAT FPUL,DRn
  lds r1, fpul
  float fpul, dr2
  rts 
  nop

# REGISTER_OUT dr2 0x4010000000000000
