# REGISTER_IN r0 0x00ffffff
# REGISTER_IN r1 0xffffff00
# REGISTER_IN r2 0x000000fc

  .text
  .global start
start:
# XOR     Rm,Rn
  xor r0, r1
# XOR     #imm,R0
  xor #0xff, r0
  rts 
  nop

# REGISTER_OUT r0 0x00ffff00
# REGISTER_OUT r1 0xff0000ff
