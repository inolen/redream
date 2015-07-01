# REGISTER_IN fr0 0xc0200000
# REGISTER_IN fr1 0x41200000

  .text
  .global start
start:
# FDIV FRm,FRn
  fdiv fr0, fr1
  rts 
  nop

# REGISTER_OUT fr1 0xc0800000
