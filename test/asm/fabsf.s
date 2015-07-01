# REGISTER_IN fr1 0xc0800000

  .text
  .global start
start:
# FABS FRn
  fabs fr1
  rts 
  nop

# REGISTER_OUT fr1 0x40800000
