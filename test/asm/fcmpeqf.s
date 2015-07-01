# REGISTER_IN fr0 0x40400000
# REGISTER_IN fr1 0xbf800000
# REGISTER_IN fr2 0xbf800000

  .text
  .global start
start:
# FCMP/EQ FRm,FRn
  fcmp/eq fr0, fr1
  movt r0
  fcmp/eq fr1, fr2
  movt r1
  rts 
  nop

# REGISTER_OUT r0 0
# REGISTER_OUT r1 1
