# REGISTER_IN fr0 0x40400000
# REGISTER_IN fr1 0xbf800000

  .text
  .global start
start:
# FCMP/GT FRm,FRn
  fcmp/gt fr0, fr1
  movt r0
  fcmp/gt fr1, fr0
  movt r1
  rts 
  nop

# REGISTER_OUT r0 0
# REGISTER_OUT r1 1
