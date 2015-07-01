# REGISTER_IN fr0 0x40400000
# REGISTER_IN fr1 0xbf800000

  .text
  .global start
start:
# FSUB FRm,FRn
  fsub fr0, fr1
  rts 
  nop

# REGISTER_OUT fr1 0xc0800000
