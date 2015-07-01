# REGISTER_IN r0 0x700000f0
# REGISTER_IN r1 0x700000f1
# REGISTER_IN r2 -4
# REGISTER_IN r3 0

  .text
  .global start
start:
# NEG     Rm,Rn
  neg r2, r4
# NEGC    Rm,Rn - T(0)
  ldc r0, SR
  negc r2, r5
  stc SR, r6
# NEGC    Rm(0),Rn - T(0)
  ldc r0, SR
  negc r3, r7
  stc SR, r8
# NEGC    Rm,Rn - T(1)
  ldc r1, SR
  negc r2, r9
  stc SR, r10
# NEGC    Rm(0),Rn - T(1)
  ldc r1, SR
  negc r3, r11
  stc SR, r12
  rts 
  nop

# REGISTER_OUT r4 4
# REGISTER_OUT r5 4
# REGISTER_OUT r6 0x700000f1
# REGISTER_OUT r7 0
# REGISTER_OUT r8 0x700000f0
# REGISTER_OUT r9 3
# REGISTER_OUT r10 0x700000f1
# REGISTER_OUT r11 0xffffffff
# REGISTER_OUT r12 0x700000f1
