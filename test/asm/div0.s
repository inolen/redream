# REGISTER_IN r0 0x700000f0
# REGISTER_IN r2 0x2
# REGISTER_IN r3 0x4
# REGISTER_IN r5 0xfffffffe
# REGISTER_IN r6 0xfffffffc
# REGISTER_IN r8 0x2
# REGISTER_IN r9 0xfffffffc
# REGISTER_IN r11 0xfffffffe
# REGISTER_IN r12 0x4

  .text
  .global start
start:
# div0u
  ldc r0, SR
  div0u
  stc SR, r1
  and r0, r1
# div0s (negative dividend / divisor)
  ldc r0, SR
  div0s r2, r3
  stc SR, r4
# div0s (positive dividend / divisor)
  ldc r0, SR
  div0s r5, r6
  stc SR, r7
# div0s (negative dividend / positive divisor)
  ldc r0, SR
  div0s r8, r9
  stc SR, r10
# div0s (positive dividend / negative divisor)
  ldc r0, SR
  div0s r11, r12
  stc SR, r13
  rts 
  nop

# REGISTER_OUT r1 0x700000f0
# REGISTER_OUT r4 0x700000f0
# REGISTER_OUT r7 0x700003f0
# REGISTER_OUT r10 0x700001f1
# REGISTER_OUT r13 0x700002f1
