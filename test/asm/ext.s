# REGISTER_IN r0 0xff
# REGISTER_IN r1 0xffff
# REGISTER_IN r2 0xfffa3002

  .text
  .global start
start:
# EXTS.B  Rm,Rn
  exts.b r0, r3
# EXTS.W  Rm,Rn
  exts.w r1, r4
# EXTU.B  Rm,Rn
  extu.b r2, r5
# EXTU.W  Rm,Rn
  extu.w r2, r6
  rts 
  nop

# REGISTER_OUT r3 0xffffffff
# REGISTER_OUT r4 0xffffffff
# REGISTER_OUT r5 0x2
# REGISTER_OUT r6 0x3002
