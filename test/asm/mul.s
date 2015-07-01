# REGISTER_IN r0 0xfffffffe
# REGISTER_IN r1 0x00005555
# REGISTER_IN r2 0xfffffffe
# REGISTER_IN r3 0x00005555
# REGISTER_IN r4 0x00000002
# REGISTER_IN r5 0xffffaaaa

  .little
  .text
  .global start
start:
# MUL.L   Rm,Rn
  mul.l r0, r1
  sts macl, r1
# MULS    Rm,Rn
  muls r2, r3
  sts macl, r3
# MULU    Rm,Rn
#  mulu r4, r5
#  sts macl, r5
  rts
  nop

# REGISTER_OUT r1 0xffff5556
# REGISTER_OUT r3 0xffff5556
# REGISTER_OT r5 0x00015554
