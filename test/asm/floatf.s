# REGISTER_IN r0 0x00000002

  .text
  .global start
start:
  lds r0, fpul
  float fpul, fr3
  rts 
  nop

# REGISTER_OUT fr3 0x40000000
