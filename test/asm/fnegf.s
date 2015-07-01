# REGISTER_IN fr0 0x40800000

  .text
  .global start
start:
# FNEG FRn
  fneg fr0
  rts 
  nop

# REGISTER_OUT fr0 0xc0800000
